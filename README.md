# Thimble

Thimble is a small embeddable scripting language for adding configuration,
application rules and controlled runtime behaviour to C++ programs without
bringing in a large scripting runtime.

It is written for C++17, is header-only, and uses only the C++ standard library.
The host application decides which values, functions and C++ objects a script
can use.

Project website: [thimblelang.org](https://thimblelang.org)  
Language specification: [SPEC.md](SPEC.md)  
License: [MIT](LICENSE)

## What is available

The current implementation provides:

- `null`, `bool`, `int`, `real` and `string`
- arrays and maps
- `let` and `var` bindings
- lexical scopes
- `if`, `else`, `while` and `return`
- named functions and recursion
- arithmetic, comparison and logical operators
- zero-based indexing
- `len`, `push`, `pop` and `remove`
- typed C++ free-function and member-function bindings
- opaque, shared-owned C++ host objects
- registered object properties and methods
- structured compile and runtime errors
- execution-step and call-depth limits

There are no implicit conversions. A script has no direct file, network,
process or operating-system access.

## A small script

```thimble
fn clamp(value, low, high) {
    if (value < low) { return low; }
    if (value > high) { return high; }
    return value;
}

var values = [10, 20, 30];
push(values, 40);

return clamp(values[2], 0, 25);
```

## Embedding

```cpp
#include "thimble/thimble.hpp"

int main() {
    thimble::HostContext host;
    host.bind_value("limit", 10);

    auto program = thimble::compile(R"(
        var n = 0;
        while (n < limit) { n = n + 1; }
        return n;
    )", host);

    if (!program) {
        return 1;
    }

    auto result = program.value().execute(host);
    if (!result) {
        return 1;
    }

    return result.value().as_int().value() == 10 ? 0 : 1;
}
```

Compilation produces an immutable `Program`. The same program can be executed
again with changed host values. Each execution receives fresh script state.

## Binding C++ functions

Existing typed free functions can be registered directly:

```cpp
int add(int a, int b) {
    return a + b;
}

host.bind_function("add", add);
```

Member functions can be registered on an existing object:

```cpp
Meter meter;
host.bind_method("scale", meter, &Meter::scale);
```

The typed convenience layer supports `bool`, `int`, `std::int64_t`, `double`,
`std::string`, `thimble::Value`, `void` and
`thimble::Result<thimble::Value>` returns. The lower-level callback API is also
available when custom conversion or validation is needed.

## Binding C++ objects

Objects are exposed through explicit descriptors. There is no C++ reflection.

```cpp
auto world = std::make_shared<GeometryWorld>();
auto world_type = host.define_object_type<GeometryWorld>("GeometryWorld");

world_type.property("total_area", &GeometryWorld::total_area);
world_type.method("move_circle", &GeometryWorld::move_circle);
world_type.method("collision_count", &GeometryWorld::collision_count);

host.bind_object("world", world, world_type);
```

The script can then use:

```thimble
world.move_circle(0, 0.25, -0.10);

if (world.collision_count() > 0) {
    return world.total_area;
}
```

See the complete [geometry example](examples/) for a C++ geometry model
controlled by a Thimble script.

## Building and testing

The repository uses Python for the build orchestration, so no shell script is
needed:

```text
python3 build.py
```

The build script compiles and runs the language tests, validates the generated
single-header distribution, and runs the geometry example. Set `CXX` if a
specific compiler is needed.

To generate only the distributable header:

```text
python3 tools/amalgamate.py
```

This writes `dist/thimble.hpp`. The generated header should not be edited by
hand.

## Repository layout

```text
include/thimble/       Development headers
tests/                 Language and embedding tests
examples/              Runnable C++ and Thimble examples
site/                  Public static website
docs/                  Contributor documentation
tools/                 Amalgamation tools
```

GitHub Actions tests Ubuntu, Windows with MSVC, and macOS on every push and
pull request. The workflow is in `.github/workflows/ci.yml`.

## Current exclusions

The language does not yet include script-defined classes or records, modules,
language exceptions, concurrency, file or network access, reflection, or
garbage collection. Host objects are available only through explicitly
registered descriptors.
