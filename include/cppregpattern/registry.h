/** Interface file for the Registry class template
 *
 *  \file registry.h
 *  \author Dr. Philip Salvaggio (salvaggio.philip@gmail.com)
 *  \date 01 Sep 2020
 */

#pragma once

#include <functional>
#include <type_traits>
#include <unordered_map>

#if __cplusplus >= 201703L
#include <optional>
#endif

namespace registry {

enum class MissingKeyPolicy {
  exception = 0,
  default_construct = 1
#if __cplusplus >= 201703L
  ,
  optional = 2
#endif
};

/** A self-registering map of functions, allowing for dynamic dispatching based
 *  on some identifier. Example usages may be constructing a subclass or using
 *  an appropriate I/O function based on an enum value or a string key. All
 *  functions in the map need to have the same signature in this implementation.
 *  Multiple threads can read from the map using Dispatch() at the same time,
 *  but do not call Register() and Dispatch() from different threads as the
 *  same time.
 *
 *  \par
 *  Since the Registry template instantiation can be quite verbose and
 *  registration is a bit of boilerplate, it is recommended that users create
 *  a type alias and a macro for registration. For example:
 *
 *  \code{.cpp}
 *  using BaseRegistry = Registry<std::string,
 *                                std::unique_ptr<Base>(int, double)>;
 *  #define REGISTER_BASE_SUBCLASS(subclass, identifier)             \
 *      static bool _registered_##subclass = BaseRegistry::Register( \
 *          #subclass, [](int a, double b) {                         \
 *              return std::unique_ptr<Base>(new subclass(a, b));    \
 *          });
 *  \endcode
 *
 *  The following policies for missing keys are supported:
 *  1. `MissingKeyPolicy::exception` - throws the `std::out_of_range` from
 *     `unordered_map`'s `at()` when the key does not exist.
 *  2. `MissingKeyPolicy::default_construct` - returns a default-constructed
 *     object when the key does not exist.
 *  3. `MissingKeyPolicy::optional` - `Dispatch` now returns a `std::optional`
 *     of `Func`'s return type. This is only supported when C++17 is enabled.
 *
 *  \note
 *  NOTE: If the registration is taking place in a STATIC library linked to the
 *  executable, then all of the headers with the registrations must be included
 *  or the library must be linked with with the platform-specific whole library
 *  flags, such as
 *  \code
 *  -Wl,--whole-archive -lmylib -Wl,--no-whole-archive
 *  \endcode
 *
 *  \tparam Key        The identifier type for the function map
 *  \tparam Func       The function signature type for the function map
 *  \tparam MKP        The behavior policy for what to do in the case of a
 *                     missing key
 *  \tparam Hash       The hash function to use for the function map
 *  \tparam KeyEqual   The key equality function for the function map
 *  \tparam Allocator  The allocator to use for the function map
 */
template <
    class Key, class Func, MissingKeyPolicy MKP = MissingKeyPolicy::default_construct,
    class Hash = std::hash<Key>, class KeyEqual = std::equal_to<Key>,
    class Allocator = std::allocator<std::pair<const Key, std::function<Func>>>>
class Registry {
 public:
  /// Function object used for constructing subclasses
  using func_t = std::function<Func>;

  /// Constructor map
  using map_t = std::unordered_map<Key, func_t, Hash, KeyEqual, Allocator>;

  Registry() = delete;
  Registry(const Registry&) = delete;
  Registry(Registry&&) noexcept = delete;
  Registry& operator=(const Registry&) = delete;
  Registry& operator=(Registry&&) noexcept = delete;

  template <class Registry>
  struct Dispatcher {};

  using dispatcher_t =
      Dispatcher<Registry<Key, Func, MKP, Hash, KeyEqual, Allocator>>;
  friend dispatcher_t;

  /// Dispatcher specialization for the exception policy
  template <class K, class F, class H, class KE, class A>
  struct Dispatcher<Registry<K, F, MissingKeyPolicy::exception, H, KE, A>> {
    using reg_t = Registry<K, F, MissingKeyPolicy::exception, H, KE, A>;
    using ret_t = typename reg_t::func_t::result_type;

    template <typename... Args>
    static ret_t Dispatch(const K& key, Args&&... args) {
      return reg_t::funcs().at(key)(std::forward<Args>(args)...);
    }
  };

  /// Dispatcher specialization for the default_construct policy
  template <class K, class F, class H, class KE, class A>
  struct Dispatcher<
      Registry<K, F, MissingKeyPolicy::default_construct, H, KE, A>> {
    using reg_t = Registry<K, F, MissingKeyPolicy::default_construct, H, KE, A>;
    using ret_t = typename reg_t::func_t::result_type;

    template <typename... Args>
    static ret_t Dispatch(const K& key, Args&&... args) {
      auto it = reg_t::funcs().find(key);
      if (it == reg_t::funcs().end()) return ret_t();
      return it->second(std::forward<Args>(args)...);
    }
  };

#if __cplusplus >= 201703L
  /// Dispatcher specialization for the optional policy
  template <class K, class F, class H, class KE, class A>
  struct Dispatcher<Registry<K, F, MissingKeyPolicy::optional, H, KE, A>> {
    using reg_t = Registry<K, F, MissingKeyPolicy::optional, H, KE, A>;
    using ret_t = std::optional<typename reg_t::func_t::result_type>;

    template <typename... Args>
    static ret_t Dispatch(const K& key, Args&&... args) {
      auto it = reg_t::funcs().find(key);
      if (it == reg_t::funcs().end()) return std::nullopt;
      return it->second(std::forward<Args>(args)...);
    }
  };
#endif

  /** Get returns the registered function from the registry with a given key.
   * Does not execute the function.
   *  \param key   The identifier passed to Register()
   *
   * \return     The registered function, or an appropriate value based on MKP:
   *             - MKP::exception: Throws std::out_of_range if the key is not found.
   *             - MKP::default_construct: Returns a default-constructed std::function<Func>.
   *             - MKP::optional (C++17+): Returns std::nullopt if the key is not found.
 
   */
  static auto Get(const Key &key) {
    if constexpr (MKP == MissingKeyPolicy::exception) {
      return funcs().at(key);
    } else if constexpr (MKP == MissingKeyPolicy::default_construct) {
      auto it = funcs().find(key);
      if (it != funcs().end()) {
        return it->second;
      } else {
        return func_t{};
      }
    } else if constexpr (MKP == MissingKeyPolicy::optional) {
#if __cplusplus >= 201703L
      auto it = funcs().find(key);
      if (it != funcs().end()) {
        return std::optional<func_t>{it->second};
      } else {
        return std::nullopt;
      }
#else
      static_assert(false,"MissingKeyPolicy::optional requires C++17 or later.");
#endif
    }
  }

  /** Retrieves all registered keys.
   *
   * \return A vector containing all keys in the registry.
   */
  static std::vector<Key> GetAll() {
    std::vector<Key> keys;
    keys.reserve(funcs().size());  // Reserve space to avoid reallocations
    for (const auto& pair : funcs()) {
        keys.push_back(pair.first);
    }
    return keys;
    }

  /** Calls one of the registered functions
   *
   *  \param key   The identifier passed to Register()
   *  \param args  Arguments to forward to the function
   *
   *  \return Result of the function
   */
  template <typename... Args>
  static typename dispatcher_t::ret_t Dispatch(const Key& key, Args&&... args) {
    return dispatcher_t::Dispatch(key, std::forward<Args>(args)...);
  }

  /** Register a function with the registry
   *
   *  \param key   The identifier under which to register this function
   *  \param func  Function to register
   *
   *  \return Whether registration is successful
   */
  static bool Register(const Key& key, const func_t& func) {
    funcs()[key] = func;
    return true;
  }

  /// Test whether the given identifier is registered
  static bool IsRegistered(const Key& key) { return funcs().count(key) == 1u; }

  /// Unregisters the given identifier
  static void Unregister(const Key& key) { funcs().erase(key); }

 private:
  static map_t& funcs() {
    static map_t func_map;
    return func_map;
  }
};
}
