#pragma once
#include "value.hpp"
#include <chrono>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace thimble {
/// Callback signature used for functions exposed to a script.
using HostFunction = std::function<Result<Value>(const std::vector<Value> &)>;

namespace detail {
template <class T> using Bare = std::decay_t<T>;
template <class T> struct IsResult : std::false_type {};
template <class T> struct IsResult<Result<T>> : std::true_type {};

template <class T> Result<Bare<T>> convert_argument(const Value &value) {
  using U = Bare<T>;
  if constexpr (std::is_same_v<U, Value>) {
    return value;
  } else if constexpr (std::is_same_v<U, bool>) {
    return value.as_bool();
  } else if constexpr (std::is_same_v<U, std::int64_t>) {
    return value.as_int();
  } else if constexpr (std::is_same_v<U, int>) {
    auto result = value.as_int();
    if (!result)
      return result.error();
    if (result.value() < std::numeric_limits<int>::min() ||
        result.value() > std::numeric_limits<int>::max()) {
      return make_error(ErrorCategory::type, "integer_range",
                        "int argument is outside the C++ int range");
    }
    return static_cast<int>(result.value());
  } else if constexpr (std::is_same_v<U, double>) {
    return value.as_real();
  } else if constexpr (std::is_same_v<U, std::string>) {
    return value.as_string();
  } else {
    static_assert(std::is_same_v<U, void>,
                  "Thimble typed bindings support bool, int, int64_t, double, "
                  "string and Value arguments");
  }
}

template <std::size_t I, class... Args>
Result<std::tuple<Bare<Args>...>>
convert_arguments(const std::vector<Value> &values) {
  using Tuple = std::tuple<Bare<Args>...>;
  if constexpr (I == sizeof...(Args)) {
    return Tuple{};
  } else {
    auto converted =
        convert_argument<std::tuple_element_t<I, std::tuple<Args...>>>(
            values[I]);
    if (!converted)
      return converted.error();
    auto rest = convert_arguments<I + 1, Args...>(values);
    if (!rest)
      return rest.error();
    auto result = std::move(rest.value());
    std::get<I>(result) = std::move(converted.value());
    return result;
  }
}

template <class R> Result<Value> convert_return(R &&result) {
  using U = Bare<R>;
  if constexpr (std::is_same_v<U, Value>) {
    return std::forward<R>(result);
  } else if constexpr (IsResult<U>::value) {
    if (!result)
      return result.error();
    return convert_return(std::move(result.value()));
  } else {
    return Value(std::forward<R>(result));
  }
}

template <class R, class F, class Tuple>
Result<Value> invoke_converted(F &&function, Tuple &arguments) {
  auto invoke = [&]() -> decltype(auto) {
    return std::apply(std::forward<F>(function), arguments);
  };
  if constexpr (std::is_void_v<R>) {
    invoke();
    return Value();
  } else {
    return convert_return(invoke());
  }
}

template <class R, class... Args>
Result<Value> invoke_free(R (*function)(Args...),
                          const std::vector<Value> &values) {
  if (values.size() != sizeof...(Args)) {
    return make_error(ErrorCategory::type, "arity_mismatch",
                      "wrong argument count");
  }
  auto converted = convert_arguments<0, Args...>(values);
  if (!converted)
    return converted.error();
  return invoke_converted<R>(function, converted.value());
}

template <class R, class C, class... Args>
Result<Value> invoke_member(R (C::*function)(Args...), C &object,
                            const std::vector<Value> &values) {
  if (values.size() != sizeof...(Args)) {
    return make_error(ErrorCategory::type, "arity_mismatch",
                      "wrong argument count");
  }
  auto converted = convert_arguments<0, Args...>(values);
  if (!converted)
    return converted.error();
  auto call = [&](auto &&...args) -> decltype(auto) {
    return std::invoke(function, object, std::forward<decltype(args)>(args)...);
  };
  return invoke_converted<R>(call, converted.value());
}

template <class R, class C, class... Args>
Result<Value> invoke_const_member(R (C::*function)(Args...) const,
                                  const C &object,
                                  const std::vector<Value> &values) {
  if (values.size() != sizeof...(Args)) {
    return make_error(ErrorCategory::type, "arity_mismatch",
                      "wrong argument count");
  }
  auto converted = convert_arguments<0, Args...>(values);
  if (!converted)
    return converted.error();
  auto call = [&](auto &&...args) -> decltype(auto) {
    return std::invoke(function, object, std::forward<decltype(args)>(args)...);
  };
  return invoke_converted<R>(call, converted.value());
}
} // namespace detail

/// Runtime descriptor for one explicitly exposed C++ object type.
struct ObjectDescriptor {
  struct Property {
    std::function<Result<Value>(const std::shared_ptr<void> &)> getter;
    std::function<Result<Value>(const std::shared_ptr<void> &, const Value &)>
        setter;
  };
  struct Method {
    std::size_t arity = 0;
    std::function<Result<Value>(const std::shared_ptr<void> &,
                                const std::vector<Value> &)>
        call;
  };
  std::string name;
  std::unordered_map<std::string, Property> properties;
  std::unordered_map<std::string, Method> methods;
};

/// Typed builder used to register properties and methods once per C++ type.
template <class T> class ObjectType {
  std::shared_ptr<ObjectDescriptor> descriptor_;
  explicit ObjectType(std::shared_ptr<ObjectDescriptor> descriptor)
      : descriptor_(std::move(descriptor)) {}
  friend class HostContext;

public:
  const std::shared_ptr<ObjectDescriptor> &descriptor() const {
    return descriptor_;
  }
  const std::string &name() const { return descriptor_->name; }

  /// Register a public C++ data member as a readable and writable property.
  template <class R> ObjectType &property(std::string name, R T::*member) {
    if (descriptor_->methods.count(name))
      throw std::logic_error("object member name is already a method");
    if (descriptor_->properties.count(name))
      throw std::logic_error("object property name is already registered");
    ObjectDescriptor::Property property;
    property.getter = [member](const std::shared_ptr<void> &instance) {
      auto object = std::static_pointer_cast<T>(instance);
      return detail::convert_return(object.get()->*member);
    };
    property.setter = [member](const std::shared_ptr<void> &instance,
                               const Value &value) {
      auto converted = detail::convert_argument<R>(value);
      if (!converted)
        return Result<Value>(converted.error());
      auto object = std::static_pointer_cast<T>(instance);
      object.get()->*member = std::move(converted.value());
      return Result<Value>(Value());
    };
    descriptor_->properties.emplace(std::move(name), std::move(property));
    return *this;
  }

  template <class R>
  ObjectType &property(std::string name, R (T::*getter)() const) {
    if (descriptor_->methods.count(name))
      throw std::logic_error("object member name is already a method");
    if (descriptor_->properties.count(name))
      throw std::logic_error("object property name is already registered");
    auto &property = descriptor_->properties[name];
    property.getter = [getter](const std::shared_ptr<void> &instance) {
      auto object = std::static_pointer_cast<T>(instance);
      return detail::convert_return((object.get()->*getter)());
    };
    return *this;
  }

  template <class R, class S>
  ObjectType &property(std::string name, R (T::*getter)() const,
                       void (T::*setter)(S)) {
    property(name, getter);
    auto &property = descriptor_->properties[name];
    property.setter = [setter](const std::shared_ptr<void> &instance,
                               const Value &value) {
      auto converted = detail::convert_argument<S>(value);
      if (!converted)
        return Result<Value>(converted.error());
      auto object = std::static_pointer_cast<T>(instance);
      (object.get()->*setter)(converted.value());
      return Result<Value>(Value());
    };
    return *this;
  }

  template <class R, class... Args>
  ObjectType &method(std::string name, R (T::*method)(Args...)) {
    if (descriptor_->properties.count(name))
      throw std::logic_error("object member name is already a property");
    if (descriptor_->methods.count(name))
      throw std::logic_error("object method name is already registered");
    descriptor_->methods[name] = {
        sizeof...(Args), [method](const std::shared_ptr<void> &instance,
                                  const std::vector<Value> &values) {
          auto object = std::static_pointer_cast<T>(instance);
          return detail::invoke_member(method, *object, values);
        }};
    return *this;
  }

  template <class R, class... Args>
  ObjectType &method(std::string name, R (T::*method)(Args...) const) {
    if (descriptor_->properties.count(name))
      throw std::logic_error("object member name is already a property");
    if (descriptor_->methods.count(name))
      throw std::logic_error("object method name is already registered");
    descriptor_->methods[name] = {
        sizeof...(Args), [method](const std::shared_ptr<void> &instance,
                                  const std::vector<Value> &values) {
          auto object = std::static_pointer_cast<const T>(instance);
          return detail::invoke_const_member(method, *object, values);
        }};
    return *this;
  }
};

/// Values and callbacks made available to one or more program executions.
class HostContext {
  std::unordered_map<std::string, Value> values_;
  std::unordered_map<std::string, std::pair<std::size_t, HostFunction>>
      functions_;

public:
  /// Bind or replace an immutable script-visible value.
  HostContext &bind_value(std::string name, Value value) {
    values_[std::move(name)] = std::move(value);
    return *this;
  }
  /// Bind or replace a fixed-arity C++ callback.
  HostContext &bind_function(std::string name, std::size_t arity,
                             HostFunction fn) {
    functions_[std::move(name)] = {arity, std::move(fn)};
    return *this;
  }
  /// Bind an ordinary free function with supported typed C++ arguments.
  template <class R, class... Args>
  HostContext &bind_function(std::string name, R (*function)(Args...)) {
    auto arity = sizeof...(Args);
    return bind_function(std::move(name), arity,
                         [function](const std::vector<Value> &values) {
                           return detail::invoke_free(function, values);
                         });
  }
  /// Bind a non-const member function on an existing object.
  /// The caller must keep the object alive while this context can be executed.
  template <class C, class R, class... Args>
  HostContext &bind_method(std::string name, C &object,
                           R (C::*function)(Args...)) {
    auto arity = sizeof...(Args);
    return bind_function(std::move(name), arity,
                         [&object, function](const std::vector<Value> &values) {
                           return detail::invoke_member(function, object,
                                                        values);
                         });
  }
  /// Bind a const member function on an existing object.
  template <class C, class R, class... Args>
  HostContext &bind_method(std::string name, const C &object,
                           R (C::*function)(Args...) const) {
    auto arity = sizeof...(Args);
    return bind_function(std::move(name), arity,
                         [&object, function](const std::vector<Value> &values) {
                           return detail::invoke_const_member(function, object,
                                                              values);
                         });
  }
  /// Bind a member function while retaining shared ownership of its object.
  template <class C, class R, class... Args>
  HostContext &bind_method(std::string name, std::shared_ptr<C> object,
                           R (C::*function)(Args...)) {
    if (!object)
      throw std::invalid_argument("cannot bind a method on a null object");
    auto arity = sizeof...(Args);
    return bind_function(std::move(name), arity,
                         [object = std::move(object),
                          function](const std::vector<Value> &values) {
                           return detail::invoke_member(function, *object,
                                                        values);
                         });
  }
  /// Bind a const member function while retaining shared ownership.
  template <class C, class R, class... Args>
  HostContext &bind_method(std::string name, std::shared_ptr<C> object,
                           R (C::*function)(Args...) const) {
    if (!object)
      throw std::invalid_argument("cannot bind a method on a null object");
    auto arity = sizeof...(Args);
    return bind_function(std::move(name), arity,
                         [object = std::move(object),
                          function](const std::vector<Value> &values) {
                           return detail::invoke_const_member(function, *object,
                                                              values);
                         });
  }
  template <class C, class R, class... Args>
  HostContext &bind_method(std::string name, std::shared_ptr<const C> object,
                           R (C::*function)(Args...) const) {
    if (!object)
      throw std::invalid_argument("cannot bind a method on a null object");
    auto arity = sizeof...(Args);
    return bind_function(std::move(name), arity,
                         [object = std::move(object),
                          function](const std::vector<Value> &values) {
                           return detail::invoke_const_member(function, *object,
                                                              values);
                         });
  }
  /// Define a reusable descriptor for an opaque C++ object type.
  template <class T> ObjectType<T> define_object_type(std::string name) {
    return ObjectType<T>(std::make_shared<ObjectDescriptor>(
        ObjectDescriptor{std::move(name), {}, {}}));
  }
  /// Bind a shared-owned object instance using a registered descriptor.
  template <class T>
  HostContext &bind_object(std::string name, std::shared_ptr<T> object,
                           const ObjectType<T> &type) {
    if (!object)
      throw std::invalid_argument("cannot bind a null host object");
    values_[std::move(name)] =
        Value::object(std::move(object), type.descriptor(), type.name());
    return *this;
  }
  /// Read-only view used by the compiler and runtime.
  const auto &values() const { return values_; }
  const auto &functions() const { return functions_; }
};
/// Safety limits for one execution. Zero means no chargeable work, not
/// unlimited.
struct Limits {
  std::size_t steps = 1000000;
  std::size_t call_depth = 256;
  std::size_t collection_size = 1000000;
  std::size_t string_size = 16 * 1024 * 1024;
  /// Cumulative estimated bytes permitted for script-created dynamic values.
  std::size_t allocation_budget = 64 * 1024 * 1024;
  /// Optional cooperative cancellation check. Returning true stops execution.
  std::function<bool()> cancelled;
  /// Wall-clock budget for an execution. Zero disables the time budget.
  std::chrono::milliseconds time_limit{0};
};
} // namespace thimble
