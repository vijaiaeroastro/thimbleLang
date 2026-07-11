#include "thimble/thimble.hpp"
#include <cassert>
#include <iostream>

int main() {
  thimble::HostContext host;
  host.bind_value("input", 7);
  host.bind_function("double_it", 1,
                     [](const std::vector<thimble::Value> &a)
                         -> thimble::Result<thimble::Value> {
                       auto x = a[0].as_int();
                       if (!x)
                         return x.error();
                       return thimble::Value(x.value() * 2);
                     });
  auto p = thimble::compile(R"(
    fn bump(x) { return x + 1; }
    var n = input;
    while (n < 10) { n = bump(n); }
    return double_it(n);
  )",
                            host);
  assert(p);
  auto result = p.value().execute(host);
  assert(result);
  assert(result.value().as_int().value() == 20);

  auto bad = thimble::compile("return true + 1;");
  assert(bad);
  auto bad_result = bad.value().execute({});
  assert(!bad_result);
  assert(bad_result.error().code == "type_mismatch");
  std::cout << "ok\n";
}
