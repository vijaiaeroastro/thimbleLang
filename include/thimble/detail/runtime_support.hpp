#pragma once

#include "../error.hpp"
#include "../value.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace thimble::detail {

struct Binding {
  Value value;
  bool mutable_ = false;
  bool initialized = true;
};

/// One lexical environment in an execution-local parent chain.
struct Environment {
  std::shared_ptr<Environment> parent;
  std::unordered_map<std::string, Binding> variables;

  explicit Environment(std::shared_ptr<Environment> parent_environment = {})
      : parent(std::move(parent_environment)) {}

  Binding *find(const std::string &name) {
    auto variable = variables.find(name);
    if (variable != variables.end())
      return &variable->second;
    return parent ? parent->find(name) : nullptr;
  }
};

/// Internal statement result. It carries normal flow, return flow or failure.
struct Flow {
  bool returned = false;
  bool failed = false;
  Value value;
  Error error;

  static Flow success() { return {}; }
};

} // namespace thimble::detail
