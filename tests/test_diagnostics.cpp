#include "test_support.hpp"

using namespace thimble;

int main() {
  auto operation = run_test_error("return 1 + true;");
  assert(operation.code == "type_mismatch");
  assert(operation.span.begin == 9);
  assert(operation.span.end == 10);
  assert(operation.span.line == 1 && operation.span.column == 10);
  assert(operation.span.end_line == 1 && operation.span.end_column == 11);

  auto condition = run_test_error("if (1) { return 2; }");
  assert(condition.code == "type_mismatch");
  assert(condition.span.begin == 4 && condition.span.end == 5);

  auto index = run_test_error("return [1, 2][4];");
  assert(index.code == "index_out_of_range");
  assert(index.span.begin == 7 && index.span.end == 16);

  HostContext host;
  host.bind_function("needs_int", 1, [](const std::vector<Value> &arguments) {
    auto value = arguments[0].as_int();
    if (!value)
      return Result<Value>(value.error());
    return Result<Value>(Value(value.value()));
  });
  auto call = run_test_error("return needs_int(true);", host);
  assert(call.span.begin == 7);
  assert(call.span.end == 22);
  assert(call.source_name == "test.thimble");
  assert(call.trace.size() == 1);
  assert(call.trace[0].function == "needs_int");

  auto standalone = Value(true).as_int();
  assert(!standalone && !standalone.error().span.valid());
  assert(format_error(standalone.error()).find("\n  at ") == std::string::npos);

  auto syntax = compile("return (1 + 2;", "broken.thimble", {});
  assert(!syntax);
  assert(syntax.error().source_name == "broken.thimble");

  auto tab = compile("\treturn @;", "tab.thimble", {});
  assert(!tab && tab.error().span.column == 9);
  auto second_line = compile("return 1;\r\nreturn @;", "crlf.thimble", {});
  assert(!second_line && second_line.error().span.line == 2 &&
         second_line.error().span.column == 8);
}
