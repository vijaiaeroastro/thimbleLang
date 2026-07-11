#include "thimble/thimble.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;
int increment(int value) { return value + 1; }

void measure(const std::string &name, const std::string &source,
             const thimble::HostContext &host, std::int64_t expected,
             int iterations = 1000) {
  const auto compile_start = Clock::now();
  auto program = thimble::compile(source, name + ".thimble", host);
  const auto compile_end = Clock::now();
  if (!program)
    throw std::runtime_error(thimble::format_error(program.error()));

  const auto execute_start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    auto result = program.value().execute(host);
    if (!result || result.value().as_int().value() != expected)
      throw std::runtime_error("benchmark produced the wrong result");
  }
  const auto execute_end = Clock::now();

  const auto compile_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              compile_end - compile_start)
                              .count();
  const auto execute_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              execute_end - execute_start)
                              .count() /
                          iterations;
  std::cout << name << ": compile " << compile_us << " us, execute "
            << execute_ns << " ns/run\n";
}
} // namespace

int main() {
  thimble::HostContext host;
  host.bind_value("input", 100);
  host.bind_function("increment", increment);

  measure("arithmetic", "return (input + 2) * 3 - 6;", host, 300);
  measure("script_call",
          "fn sum(a, b) { return a + b; } return sum(input, 20);", host, 120);
  measure("host_call", "return increment(input);", host, 101);
  measure("map_lookup",
          "let values = {\"answer\": input}; return values[\"answer\"];", host,
          100);
  measure("loop",
          "var total = 0; var i = 0; while (i <= input) { total = total + i; "
          "i = i + 1; } return total;",
          host, 5050, 250);
}
