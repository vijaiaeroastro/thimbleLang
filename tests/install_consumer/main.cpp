#include <thimble/thimble.hpp>

int main() {
  auto program = thimble::compile("return 42;");
  if (!program)
    return 1;
  auto result = program.value().execute({});
  return result && result.value().as_int().value() == 42 ? 0 : 1;
}
