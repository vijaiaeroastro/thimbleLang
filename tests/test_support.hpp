#pragma once
#include "thimble/thimble.hpp"
#include <cassert>
#include <string>

inline thimble::Value run_test_value(const std::string &source,
                                     const thimble::HostContext &host = {},
                                     thimble::Limits limits = {}) {
  auto program = thimble::compile(source, "test.thimble", host);
  assert(program);
  auto result = program.value().execute(host, std::move(limits));
  assert(result);
  return result.value();
}

inline thimble::Error run_test_error(const std::string &source,
                                     const thimble::HostContext &host = {},
                                     thimble::Limits limits = {}) {
  auto program = thimble::compile(source, "test.thimble", host);
  if (!program)
    return program.error();
  auto result = program.value().execute(host, std::move(limits));
  assert(!result);
  return result.error();
}
