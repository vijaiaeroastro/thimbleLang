# Thimble

Thimble is a tiny scripting language which can be embedded inside a C++17
program. It is meant for configuration, small rules, automation hooks and
other places where a full scripting runtime will be too much.

The language is intentionally small: `null`, `bool`, `int`, `real` and
`string`; `let` and `var`; `if`, `else`, `while` and `return`; named functions;
strict operators; and explicitly bound C++ values and functions. A script has
no direct file, network, process or operating-system access.

The language specification is in [SPEC.md](SPEC.md). The project home is
[thimblelang.org](https://thimblelang.org).

Thimble is released under the [MIT License](LICENSE).

For a map of the implementation and contribution flow, see
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

The public website is kept in `site/`. It is a static Workers Asset deployment
configured by `wrangler.jsonc`. Cloudflare Workers can serve that directory
directly, so the website does not need a separate frontend framework or build
system.

## Current implementation

The implementation is header-only and uses only the C++ standard library. The
development headers are kept separately under `include/thimble/`. For shipping,
the amalgamation script joins them into one header:

```text
python3 tools/amalgamate.py
```

This writes `dist/thimble.hpp`. The generated file is convenient to copy into a
host project. It should not be edited by hand.

## Small example

```cpp
#include "thimble/thimble.hpp"

int main() {
    thimble::HostContext host;
    host.bind_value("limit", 10);
    host.bind_function("log", 1, [](const std::vector<thimble::Value>& args) {
        // Send args[0] to the application's logger here.
        return thimble::Result<thimble::Value>(thimble::Value());
    });

    auto program = thimble::compile(R"(
        var n = 0;
        while (n < limit) { n = n + 1; }
        log("finished");
        return n;
    )", host);

    if (!program) return 1;
    auto result = program.value().execute(host);
    return result ? 0 : 1;
}
```

The same compiled `Program` can be executed again with changed host values.
Execution has step and call-depth limits so that a host can keep untrusted
scripts bounded.

## Building and testing

The repository does not need a package manager. Run:

```text
python3 build.py
```

The script compiles the test suite with C++17, runs it, creates the amalgamated
header, and validates that generated header separately. A compiler can be
selected with the `CXX` environment variable, for example `CXX=clang++
python3 build.py`.

GitHub Actions runs the same build on Ubuntu, Windows (MSVC), and macOS for
every push and pull request. The workflow is in `.github/workflows/ci.yml`.

## Project direction

The first release is keeping the language plain and dependable. Classes,
modules, exceptions, objects, concurrency, file/network access and garbage
collection are deliberately kept out of version 0.1. New features should first
be written into the specification and then covered by tests before being added
to the runtime.
