#include "thimble.hpp"

int main() {
    thimble::HostContext host;
    auto program = thimble::compile("return 42;", host);
    if (!program) return 1;
    auto result = program.value().execute(host);
    return result && result.value().as_int().value() == 42 ? 0 : 1;
}
