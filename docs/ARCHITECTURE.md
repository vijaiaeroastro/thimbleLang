# Thimble code guide

This note is for somebody joining the project and trying to find the right file
for a change. The language rules remain in [SPEC.md](../SPEC.md); this file only
explains how the current code is arranged.

## Header layout

`include/thimble/thimble.hpp` is the public entry point. It includes the small
headers below.

- `error.hpp` contains `Span`, `Error`, and the exception-free `Result<T>` used
  by all public operations.
- `value.hpp` contains the tagged, owning `Value` type and checked accessors.
- `host.hpp` contains `HostContext`, callback registration, and `Limits`.
- `lexer.hpp` turns source bytes into tokens. It owns its input string and keeps
  byte offsets plus line and column counters for diagnostics.
- `ast.hpp` contains the compiler-owned expression, statement and function
  nodes. Child nodes are held by `std::unique_ptr`, so there is one clear owner
  for every parsed node.
- `parser.hpp` is a recursive-descent parser. Each precedence level has a
  separate method, and parser results are moved into the AST.
- `runtime.hpp` contains `Program`, compile-time name checks, lexical
  environments, expression evaluation, statement execution, callbacks and
  limits.

The runtime uses `std::shared_ptr<Env>` only for parent scope lifetime. A block
gets a child environment, and a function call gets a child environment rooted
at the current program execution. The `Program` itself remains immutable and
can be executed again, or by another thread with a separate host context.

## Compile and execute flow

`compile(source, host)` performs these steps:

1. `Lexer::run()` creates tokens or a lexical diagnostic.
2. `Parser::parse()` creates owned AST nodes or a syntax diagnostic.
3. Function names, parameter duplicates, variable names, callable names and
   callable arities are checked.
4. The resulting `Program` takes ownership of the AST and source name/text.

`Program::execute(host, limits)` then creates a fresh root environment, copies
host values into immutable bindings, and runs top-level statements. Every
expression and statement goes through the runner's step counter. Script calls
also check call depth. A `return` carries a value through `Flow`; errors carry
the structured `Error` without using C++ exceptions.

## Adding a language feature

For a new syntax feature, update the specification first, then normally touch
these places:

1. add a token in `lexer.hpp` if needed;
2. add an AST kind or field in `ast.hpp`;
3. parse it in `parser.hpp`;
4. validate names and arity in `runtime.hpp`'s compile pass;
5. execute it in `Runner::eval` or `Runner::exec`; and
6. add both successful and failing cases in `tests/test_language.cpp`.

Run `python3 build.py` after each change. It runs the language tests, regenerates an
amalgamated header, and compiles a separate test against that generated file.

## Amalgamation

`tools/amalgamate.py` recursively expands only quoted local Thimble includes.
It leaves standard-library includes untouched and removes repeated `#pragma once`
lines. The generated header is disposable output; edit the files under
`include/thimble/` and run the script again.
