#pragma once
#include "ast.hpp"
#include "detail/runtime_support.hpp"
#include "host.hpp"
#include "parser.hpp"
#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace thimble {
using detail::Binding;
using detail::Environment;
using detail::Flow;

class Program {
  std::string source_;
  std::string source_name_;
  std::vector<std::unique_ptr<Stmt>> top_;
  std::vector<Function> funcs_;
  std::unordered_map<std::string, std::size_t> fidx_;
  std::unordered_set<std::string> required_host_values_;
  std::unordered_map<std::string, std::size_t> required_host_functions_;
  struct ObjectShape {
    std::string type_name;
    std::unordered_map<std::string, bool> properties;
    std::unordered_map<std::string, std::size_t> methods;
  };
  std::unordered_map<std::string, ObjectShape> required_objects_;
  friend Result<Program> compile(std::string, const HostContext &);
  friend Result<Program> compile(std::string, std::string, const HostContext &);
  struct Runner {
    const Program &p;
    const HostContext &host;
    Limits lim;
    std::size_t steps = 0;
    std::size_t allocation_charged = 0;
    std::chrono::steady_clock::time_point started =
        std::chrono::steady_clock::now();
    std::shared_ptr<Environment> root;
    Runner(const Program &pp, const HostContext &hh, Limits ll)
        : p(pp), host(hh), lim(ll) {}
#include "detail/collections_objects.inl"
#include "detail/evaluator.inl"
#include "detail/execution.inl"
#include "detail/runner_control.inl"
  };

public:
  Program() = default;
  Program(Program &&) = default;
  Program &operator=(Program &&) = default;
  Program(const Program &) = delete;
  /// Execute with fresh script state and the supplied host context.
  Result<Value> execute(const HostContext &host, Limits limits = {}) const {
    for (const auto &name : required_host_values_)
      if (!host.values().count(name))
        return make_error(ErrorCategory::name, "incompatible_host_interface",
                          "required host value is missing: " + name, {},
                          source_);
    for (const auto &function : required_host_functions_) {
      auto found = host.functions().find(function.first);
      if (found == host.functions().end() ||
          found->second.first != function.second)
        return make_error(ErrorCategory::name, "incompatible_host_interface",
                          "host function changed: " + function.first, {},
                          source_);
    }
    for (const auto &required : required_objects_) {
      auto value = host.values().find(required.first);
      if (value == host.values().end() ||
          value->second.type() != Type::host_object)
        return make_error(ErrorCategory::name, "incompatible_host_interface",
                          "host object changed: " + required.first, {},
                          source_);
      auto object = value->second.as_object().value();
      auto descriptor =
          std::static_pointer_cast<ObjectDescriptor>(object->descriptor);
      const auto &shape = required.second;
      if (object->type_name != shape.type_name ||
          descriptor->properties.size() != shape.properties.size() ||
          descriptor->methods.size() != shape.methods.size())
        return make_error(ErrorCategory::name, "incompatible_host_interface",
                          "host object shape changed: " + required.first, {},
                          source_);
      for (const auto &property : shape.properties) {
        auto found = descriptor->properties.find(property.first);
        if (found == descriptor->properties.end() ||
            static_cast<bool>(found->second.setter) != property.second)
          return make_error(ErrorCategory::name, "incompatible_host_interface",
                            "host object property changed: " + required.first +
                                "." + property.first,
                            {}, source_);
      }
      for (const auto &method : shape.methods) {
        auto found = descriptor->methods.find(method.first);
        if (found == descriptor->methods.end() ||
            found->second.arity != method.second)
          return make_error(ErrorCategory::name, "incompatible_host_interface",
                            "host object method changed: " + required.first +
                                "." + method.first,
                            {}, source_);
      }
    }
    try {
      Runner r(*this, host, limits);
      return r.run();
    } catch (const std::bad_alloc &) {
      return make_error(ErrorCategory::limit, "allocation_failure",
                        "C++ allocation failed during execution", {}, source_,
                        source_name_);
    }
  }
};
#include "detail/compiler.hpp"
} // namespace thimble
