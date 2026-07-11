#include "thimble/thimble.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <string>

using namespace thimble;

static Value run_value(const std::string &source, const HostContext &host = {},
                       Limits limits = {}) {
  auto program = compile(source, host);
  assert(program && "source failed to compile");
  auto result = program.value().execute(host, limits);
  assert(result && "program failed to execute");
  return result.value();
}

static Error run_error(const std::string &source, const HostContext &host = {},
                       Limits limits = {}) {
  auto program = compile(source, host);
  if (!program)
    return program.error();
  auto result = program.value().execute(host, limits);
  assert(!result && "program was expected to fail");
  return result.error();
}

struct Calculator {
  int calls = 0;
  Result<Value> add(const std::vector<Value> &args) {
    ++calls;
    auto a = args[0].as_int();
    auto b = args[1].as_int();
    if (!a)
      return a.error();
    if (!b)
      return b.error();
    return Value(a.value() + b.value());
  }
};

int plain_add(int a, int b) { return a + b; }

std::string plain_greet(const std::string &name) { return "Hello, " + name; }

thimble::Result<thimble::Value> identity_value(thimble::Value value) {
  return value;
}

thimble::Result<int> checked_double(int value) {
  if (value < 0)
    return thimble::make_error(thimble::ErrorCategory::host, "negative_value",
                               "value must not be negative");
  return value * 2;
}

struct Multiplier {
  int factor = 3;
  int multiply(int value) { return value * factor; }
  int read_factor() const { return factor; }
};

struct Request {
  int amount = 5;
  std::string label = "new";
  int get_amount() const { return amount; }
  void set_amount(int value) { amount = value; }
  int double_value(int value) const { return value * 2; }
};

int main() {
  static_assert(version_major == 0 && version_minor == 1 && version_patch == 0);
  assert(std::string(version_string) == "0.1.0");
  // Literals, precedence, grouping and return defaults.
  assert(run_value("return null;").is_null());
  assert(run_value("return true || false && false;").as_bool().value());
  assert(run_value("return (2 + 3) * 4 - 5;").as_int().value() == 15);
  assert(run_value("return 7 / 2;").as_int().value() == 3);
  assert(run_value("return 7 % 2;").as_int().value() == 1);
  assert(run_value("return \"a\" + \"\\n\" + \"b\";").as_string().value() ==
         "a\nb");
  assert(run_value("return 2.5 * 2.0;").as_real().value() == 5.0);
  assert(run_value("return 3 < 4 && 4 <= 4 && 5 > 4 && 5 >= 5;")
             .as_bool()
             .value());
  assert(run_value("return \"a\" < \"b\";").as_bool().value());
  assert(run_value("return !false;").as_bool().value());
  assert(run_value("// hello\n/* block */ return \"\\x41\\0\";")
             .as_string()
             .value() == std::string("A\0", 2));
  assert(run_error("return 1 == 1.0;").code == "type_mismatch");
  assert(run_value("return -2.5 < -2.0;").as_bool().value());
  assert(run_value("return null == null;").as_bool().value());
  assert(!run_value("return \"a\" != \"a\";").as_bool().value());

  // Variables, shadowing and fresh state on repeated execution.
  HostContext host;
  host.bind_value("input", 4);
  auto repeat = compile("var n = input; n = n + 1; return n;", host);
  assert(repeat);
  assert(repeat.value().execute(host).value().as_int().value() == 5);
  host.bind_value("input", 9);
  assert(repeat.value().execute(host).value().as_int().value() == 10);
  assert(run_value("let x = 2; { let x = 5; } return x;").as_int().value() ==
         2);
  assert(run_value("var x = 2; x = \"two\"; return x;").as_string().value() ==
         "two");

  // Control flow and user functions, including recursion and mutual calls.
  assert(run_value(R"(
        fn fact(n) { if (n <= 1) { return 1; } return n * fact(n - 1); }
        return fact(6);
    )")
             .as_int()
             .value() == 720);
  assert(run_value(R"(
        fn even(n) { if (n == 0) { return true; } return odd(n - 1); }
        fn odd(n) { if (n == 0) { return false; } return even(n - 1); }
        return even(10);
    )")
             .as_bool()
             .value());
  assert(run_value("fn empty() {} return empty();").is_null());

  // Short circuiting must avoid the host call.
  int calls = 0;
  HostContext short_host;
  short_host.bind_function("side_effect", 0,
                           [&](const std::vector<Value> &) -> Result<Value> {
                             ++calls;
                             return Value(true);
                           });
  assert(!run_value("return false && side_effect();", short_host)
              .as_bool()
              .value());
  assert(
      run_value("return true || side_effect();", short_host).as_bool().value());
  assert(calls == 0);

  // Free-function style binding and an existing C++ member method.
  Calculator calc;
  HostContext method_host;
  method_host.bind_function(
      "add", 2, [&calc](const std::vector<Value> &a) { return calc.add(a); });
  assert(run_value("return add(3, 4);", method_host).as_int().value() == 7);
  assert(calc.calls == 1);
  std::function<Result<Value>(const std::vector<Value> &)> free_function =
      [](const std::vector<Value> &a) -> Result<Value> {
    return Value(static_cast<std::int64_t>(a.size()));
  };
  HostContext free_host;
  free_host.bind_function("argc", 2, free_function);
  assert(run_value("return argc(1, 2);", free_host).as_int().value() == 2);
  HostContext typed_host;
  typed_host.bind_function("plain_add", plain_add);
  typed_host.bind_function("plain_greet", plain_greet);
  typed_host.bind_function("identity", identity_value);
  typed_host.bind_function("checked_double", checked_double);
  Multiplier multiplier;
  typed_host.bind_method("multiply", multiplier, &Multiplier::multiply);
  typed_host.bind_method("read_factor", multiplier, &Multiplier::read_factor);
  assert(run_value("return plain_add(multiply(4), read_factor());", typed_host)
             .as_int()
             .value() == 15);
  assert(run_value("return plain_greet(\"Vijai\");", typed_host)
             .as_string()
             .value() == "Hello, Vijai");
  assert(run_value("return identity(12);", typed_host).as_int().value() == 12);
  assert(run_value("return checked_double(6);", typed_host).as_int().value() ==
         12);
  assert(run_error("return checked_double(-1);", typed_host).code ==
         "negative_value");

  HostContext object_host;
  auto request = std::make_shared<Request>();
  auto request_type = object_host.define_object_type<Request>("Request");
  request_type.property("amount", &Request::get_amount, &Request::set_amount);
  request_type.property("read_amount", &Request::get_amount);
  request_type.property("label", &Request::label);
  request_type.method("double_value", &Request::double_value);
  object_host.bind_object("request", request, request_type);
  assert(run_value(R"(
        var values = [1, 2];
        push(values, 3);
        values[1] = 4;
        let settings = {"enabled": true, 1: "one"};
        request.amount = 7;
        request.label = "ready";
        return request.double_value(values[0]) + len(settings) + request.amount;
    )",
                   object_host)
             .as_int()
             .value() == 11);
  assert(request->amount == 7);
  assert(request->label == "ready");
  assert(!compile("return request.missing;", object_host));
  assert(!compile("return request.double_value();", object_host));
  assert(!compile("request.read_amount = 4;", object_host));
  auto object_program = compile("return request.double_value(4);", object_host);
  assert(object_program);
  HostContext missing_object_host;
  assert(object_program.value().execute(missing_object_host).error().code ==
         "incompatible_host_interface");
  HostContext compatible_object_host;
  auto other_request = std::make_shared<Request>();
  auto compatible_type =
      compatible_object_host.define_object_type<Request>("Request");
  compatible_type.property("amount", &Request::get_amount,
                           &Request::set_amount);
  compatible_type.property("read_amount", &Request::get_amount);
  compatible_type.property("label", &Request::label);
  compatible_type.method("double_value", &Request::double_value);
  compatible_object_host.bind_object("request", other_request, compatible_type);
  assert(object_program.value()
             .execute(compatible_object_host)
             .value()
             .as_int()
             .value() == 8);

  auto typed_program = compile("return checked_double(3);", typed_host);
  assert(typed_program);
  HostContext changed_function_host;
  changed_function_host.bind_function(
      "checked_double", 2,
      [](const std::vector<Value> &) { return Result<Value>(Value(0)); });
  assert(typed_program.value().execute(changed_function_host).error().code ==
         "incompatible_host_interface");
  bool duplicate_member_rejected = false;
  try {
    request_type.method("double_value", &Request::double_value);
  } catch (const std::logic_error &) {
    duplicate_member_rejected = true;
  }
  assert(duplicate_member_rejected);

  bool null_object_rejected = false;
  try {
    object_host.bind_object("null_request", std::shared_ptr<Request>{},
                            request_type);
  } catch (const std::invalid_argument &) {
    null_object_rejected = true;
  }
  assert(null_object_rejected);
  assert(run_error("let values = [1]; push(values, 2);", object_host).code ==
         "immutable_assignment");
  auto callback_failure = HostContext{};
  callback_failure.bind_function(
      "fail", 0, [](const std::vector<Value> &) -> Result<Value> {
        return make_error(ErrorCategory::host, "application_failure",
                          "application rejected call");
      });
  assert(run_error("fail();", callback_failure).code == "application_failure");
  assert(run_error("argc(1);", free_host).code == "arity_mismatch");

  // Type, mutability, arithmetic, name and limit failures.
  assert(run_error("return true + 1;").code == "type_mismatch");
  assert(run_error("let x = 1; x = 2;").code == "immutable_assignment");
  assert(run_error("return 1 / 0;").code == "division_by_zero");
  assert(run_error("return 9223372036854775807 + 1;").code ==
         "integer_overflow");
  assert(run_error("return missing;").code == "unknown_name");
  Limits short_run;
  short_run.steps = 100;
  short_run.call_depth = 64;
  assert(run_error("while (true) { }", {}, short_run).category ==
         ErrorCategory::limit);
  Limits shallow_calls;
  shallow_calls.steps = 10000;
  shallow_calls.call_depth = 4;
  assert(run_error("fn loop() { return loop(); } return loop();", {},
                   shallow_calls)
             .code == "call_depth_limit");
  Limits cancelled;
  std::size_t cancellation_checks = 0;
  cancelled.cancelled = [&] { return ++cancellation_checks >= 5; };
  assert(run_error("while (true) {}", {}, cancelled).code == "cancelled");
  Limits timed;
  timed.steps = 100000000;
  timed.time_limit = std::chrono::milliseconds(1);
  assert(run_error("while (true) {}", {}, timed).code == "time_limit");
  assert(run_error("return 1.0 / 0.0;").code == "division_by_zero");
  auto traced = run_error(R"(
        fn inner() { return 1 / 0; }
        fn outer() { return inner(); }
        return outer();
    )");
  assert(traced.trace.size() == 2);
  assert(traced.trace[0].function == "inner");
  assert(traced.trace[1].function == "outer");
  auto rendered = format_error(traced);
  assert(rendered.find("division by zero") != std::string::npos);
  assert(rendered.find("called from inner") != std::string::npos);

  Limits small_collections;
  small_collections.steps = 1000;
  small_collections.call_depth = 32;
  small_collections.collection_size = 1;
  small_collections.string_size = 1024;
  assert(run_error("var a = [1]; push(a, 2);", {}, small_collections).code ==
         "collection_limit");
  auto cycle_a = Value::array({});
  auto cycle_b = Value::array({});
  cycle_a.as_array().value()->values.push_back(cycle_a);
  cycle_b.as_array().value()->values.push_back(cycle_b);
  assert(!cycle_a.is_acyclic());
  assert(cycle_a == cycle_b);
  cycle_a.as_array().value()->values.clear();
  cycle_b.as_array().value()->values.clear();
  assert(run_error("var a = []; push(a, a);").code == "cyclic_collection");
  Limits small_strings;
  small_strings.steps = 1000;
  small_strings.call_depth = 32;
  small_strings.collection_size = 10;
  small_strings.string_size = 3;
  assert(run_error("return \"ab\" + \"cd\";", {}, small_strings).code ==
         "string_limit");
  Limits small_memory;
  small_memory.allocation_budget = 3;
  assert(run_error("return \"four\";", {}, small_memory).code ==
         "allocation_limit");
  HostContext non_finite;
  non_finite.bind_value("bad", Value(std::numeric_limits<double>::infinity()));
  assert(run_error("return bad;", non_finite).code == "non_finite_real");
  HostContext throws;
  throws.bind_function(
      "throws", 0,
      [](const std::vector<Value> &) -> Result<Value> { throw 1; });
  assert(run_error("throws();", throws).code == "host_failure");
  assert(!Value(1).as_string());

  // Lexical and syntax diagnostics are returned by compilation.
  auto lexical = compile("return @;");
  assert(!lexical && lexical.error().category == ErrorCategory::lexical);
  auto named_lexical = compile("return @;", "rules/access.thimble", {});
  assert(!named_lexical);
  assert(named_lexical.error().source_name == "rules/access.thimble");
  assert(named_lexical.error().span.end_column >
         named_lexical.error().span.column);
  assert(format_error(named_lexical.error()).find("rules/access.thimble:1:8") !=
         std::string::npos);
  auto escape = compile("return \"\\q\";");
  assert(!escape && escape.error().code == "invalid_escape");
  auto syntax = compile("if (true) return 1;");
  assert(!syntax && syntax.error().category == ErrorCategory::syntax);
  auto duplicate = compile("fn f() {} fn f() {} return null;");
  assert(!duplicate && duplicate.error().code == "duplicate_name");

  std::cout << "All Thimble language tests passed.\n";
}
