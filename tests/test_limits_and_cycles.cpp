#include "test_support.hpp"
#include <stdexcept>

using namespace thimble;

int main() {
  Limits cancelled;
  cancelled.cancelled = []() -> bool {
    throw std::runtime_error("cancel check failed");
  };
  assert(run_test_error("return 1;", {}, cancelled).code == "host_failure");

  Limits no_steps;
  no_steps.steps = 0;
  assert(run_test_error("return 1;", {}, no_steps).code == "step_limit");

  Limits no_allocations;
  no_allocations.allocation_budget = 0;
  assert(run_test_error("return \"x\";", {}, no_allocations).code ==
         "allocation_limit");

  assert(run_test_error("var a = []; push(a, a);").code == "cyclic_collection");
  assert(run_test_error("var a = [0]; var b = [a]; a[0] = b;").code ==
         "cyclic_collection");
  assert(run_test_error("var m = {}; var a = [m]; m[\"x\"] = a;").code ==
         "cyclic_collection");
}
