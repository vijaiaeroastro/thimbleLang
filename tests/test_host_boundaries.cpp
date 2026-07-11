#include "test_support.hpp"
#include <memory>
#include <stdexcept>

using namespace thimble;

struct FaultyProperties {
  int read() const { throw std::runtime_error("private detail"); }
  void write(int) { throw std::runtime_error("private detail"); }
};

struct OwnedCounter {
  int value = 4;
  int increment(int amount) {
    value += amount;
    return value;
  }
};

int main() {
  HostContext host;
  auto faulty = std::make_shared<FaultyProperties>();
  auto type = host.define_object_type<FaultyProperties>("FaultyProperties");
  type.property("value", &FaultyProperties::read, &FaultyProperties::write);
  host.bind_object("faulty", faulty, type);

  auto getter = run_test_error("return faulty.value;", host);
  assert(getter.code == "host_failure");
  assert(getter.span.column == 8);
  assert(getter.trace.size() == 1);
  assert(getter.trace[0].function == "FaultyProperties.value");
  assert(getter.message.find("private detail") == std::string::npos);

  auto setter = run_test_error("faulty.value = 3;", host);
  assert(setter.code == "host_failure");
  assert(setter.trace.size() == 1);
  auto conversion = run_test_error("faulty.value = \"wrong\";", host);
  assert(conversion.code == "type_mismatch");
  assert(conversion.span.begin == 0 && conversion.span.end == 23);

  HostContext owned_host;
  auto counter = std::make_shared<OwnedCounter>();
  std::weak_ptr<OwnedCounter> lifetime = counter;
  owned_host.bind_method("increment", counter, &OwnedCounter::increment);
  counter.reset();
  assert(!lifetime.expired());
  assert(run_test_value("return increment(3);", owned_host).as_int().value() ==
         7);

  bool null_rejected = false;
  try {
    owned_host.bind_method("bad", std::shared_ptr<OwnedCounter>{},
                           &OwnedCounter::increment);
  } catch (const std::invalid_argument &) {
    null_rejected = true;
  }
  assert(null_rejected);
}
