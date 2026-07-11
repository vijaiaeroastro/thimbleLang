#include "thimble/thimble.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

/// libFuzzer entry point. Every byte sequence is valid input to the compiler;
/// a lexical or syntax error is an expected result, not a fuzz failure.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  (void)thimble::compile(
      std::string(reinterpret_cast<const char *>(data), size), "fuzz-input",
      {});
  return 0;
}
