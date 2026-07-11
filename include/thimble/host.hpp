#pragma once
#include "value.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace thimble {
/// Callback signature used for functions exposed to a script.
using HostFunction = std::function<Result<Value>(const std::vector<Value>&)>;
/// Values and callbacks made available to one or more program executions.
class HostContext {
  std::unordered_map<std::string,Value> values_;
  std::unordered_map<std::string,std::pair<std::size_t,HostFunction>> functions_;
public:
  /// Bind or replace an immutable script-visible value.
  HostContext& bind_value(std::string name, Value value){values_[std::move(name)]=std::move(value);return *this;}
  /// Bind or replace a fixed-arity C++ callback.
  HostContext& bind_function(std::string name,std::size_t arity,HostFunction fn){functions_[std::move(name)]={arity,std::move(fn)};return *this;}
  /// Read-only view used by the compiler and runtime.
  const auto& values()const{return values_;} const auto& functions()const{return functions_;}
};
/// Safety limits for one execution. Zero means no chargeable work, not unlimited.
struct Limits { std::size_t steps=1000000, call_depth=256; };
}
