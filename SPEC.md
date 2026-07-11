# Thimble Language Specification

- Status: proposed version 0.1
- Implementation baseline: C++17
- Last updated: 2026-07-11

## 1. Scope and goals

Thimble is a small, dynamically typed scripting language intended to be embedded
in a C++ application. A host compiles source text once into an immutable program
and may execute that program many times with different host-provided values.

The initial language is deliberately limited. It provides:

- the values `null`, `bool`, `int`, `real`, and `string`;
- immutable and mutable lexical bindings;
- conditionals and loops;
- named functions;
- expression evaluation and function calls;
- controlled access to explicitly registered host values and functions; and
- structured diagnostics and bounded execution.

Thimble has no ambient access to the process, filesystem, network, clock,
environment, random-number generator, or standard input/output. A script gains a
capability only when its host supplies a function implementing that capability.

This specification defines the source language and the observable embedding
contract. It does not prescribe an interpreter, bytecode format, internal class
layout, or exact spelling of C++ API types.

The implementation shall be header-only, shall require no dependency beyond the
C++ standard library, and shall compile as C++17. An implementation may offer
C++20 conveniences without making them necessary for the core API.

Normative terms such as **shall**, **must**, **may**, and **should** have their
usual specification meanings.

## 2. Source text and lexical structure

### 2.1 Source representation

Source is a sequence of bytes. ASCII bytes are interpreted as described below.
Non-ASCII bytes may occur inside comments and string literals and are preserved
unchanged. A non-ASCII byte anywhere else is a lexical error. Version 0.1 does
not validate or normalize Unicode and does not permit non-ASCII identifiers.

An implementation shall accept `LF` and `CRLF` line endings. Each counts as one
line break. A bare `CR` may also count as one line break. A NUL byte outside a
string literal is a lexical error.

### 2.2 Whitespace and comments

Spaces, horizontal tabs, and line breaks separate tokens and otherwise have no
meaning.

- `//` begins a comment ending immediately before the next line break or at end
  of source.
- `/*` begins a block comment ending at the next `*/`.
- Block comments do not nest.
- An unterminated block comment is a lexical error located at its opening `/*`.

Comments behave as whitespace.

### 2.3 Identifiers and keywords

An identifier has the form:

```ebnf
identifier = ( "A" ... "Z" | "a" ... "z" | "_" ),
             { "A" ... "Z" | "a" ... "z" | "0" ... "9" | "_" } ;
```

Identifiers are case-sensitive. The following spellings are reserved keywords
and cannot be used as identifiers:

```text
let  var  fn  if  else  while  return  true  false  null
```

### 2.4 Tokens

In addition to identifiers and literals, Thimble uses these tokens:

```text
(  )  {  }  ,  ;
+  -  *  /  %
=  ==  !=  <  <=  >  >=
!  &&  ||
```

Tokenization uses the longest valid token, so `!=` is one token rather than `!`
followed by `=`.

### 2.5 Literals

#### Null and Boolean literals

`null`, `true`, and `false` denote the corresponding values.

#### Integer literals

An integer literal is one or more decimal digits. There are no digit separators
or hexadecimal, octal, or binary forms. A leading sign is an operator, not part
of the literal.

The resulting value shall fit in the range `-9223372036854775808` through
`9223372036854775807`. The magnitude `9223372036854775808` is accepted only as
the direct operand of unary `-`, producing the minimum `int` value. Any other
out-of-range integer literal is a lexical error.

Leading zeroes have no special meaning.

#### Real literals

A real literal has a decimal point, an exponent, or both:

```ebnf
digits        = digit, { digit } ;
exponent      = ( "e" | "E" ), [ "+" | "-" ], digits ;
real-literal  = digits, ".", digits, [ exponent ]
              | digits, exponent ;
```

Examples are `0.0`, `12.25`, `1e3`, and `2.5e-2`. Forms such as `.5`, `1.`,
`nan`, and `inf` are not literals.

A real literal is converted, independently of the C++ locale, to the nearest
representable `double`, using round-to-nearest with ties to even where the host
implementation supports it. A literal that cannot produce a finite `double` is
a lexical error.

#### String literals

A string literal begins and ends with `"`. It may contain any non-NUL byte other
than an unescaped `"`, `\`, or line break. The following escapes are supported:

| Escape | Resulting byte |
|---|---|
| `\"` | double quote (`0x22`) |
| `\\` | backslash (`0x5c`) |
| `\n` | line feed (`0x0a`) |
| `\r` | carriage return (`0x0d`) |
| `\t` | horizontal tab (`0x09`) |
| `\0` | NUL (`0x00`) |
| `\xHH` | the byte denoted by exactly two hexadecimal digits |

All other escapes are lexical errors. Strings are byte sequences and may contain
NUL or invalid UTF-8. There is no character type, interpolation, raw-string form,
or Unicode escape in version 0.1.

## 3. Grammar

The following EBNF is normative except that lexical productions are described in
Section 2. Braces in EBNF mean repetition; braces in quoted text are source
tokens.

```ebnf
program          = { top-level-item }, end-of-source ;

top-level-item   = function-decl | statement ;

function-decl    = "fn", identifier, "(", [ parameters ], ")", block ;
parameters       = identifier, { ",", identifier } ;

statement        = variable-decl
                 | assignment-stmt
                 | expression-stmt
                 | if-stmt
                 | while-stmt
                 | return-stmt
                 | block ;

variable-decl    = ( "let" | "var" ), identifier, "=", expression, ";" ;
assignment-stmt  = identifier, "=", expression, ";" ;
expression-stmt  = expression, ";" ;

if-stmt          = "if", "(", expression, ")", block,
                   [ "else", ( block | if-stmt ) ] ;
while-stmt       = "while", "(", expression, ")", block ;
return-stmt      = "return", [ expression ], ";" ;
block            = "{", { statement }, "}" ;

expression       = logical-or ;
logical-or       = logical-and, { "||", logical-and } ;
logical-and      = equality, { "&&", equality } ;
equality         = comparison, { ( "==" | "!=" ), comparison } ;
comparison       = term, { ( "<" | "<=" | ">" | ">=" ), term } ;
term             = factor, { ( "+" | "-" ), factor } ;
factor           = unary, { ( "*" | "/" | "%" ), unary } ;
unary            = ( "!" | "-" ), unary | primary ;
primary          = literal
                 | call
                 | identifier
                 | "(", expression, ")" ;
call             = identifier, "(", [ arguments ], ")" ;
arguments        = expression, { ",", expression } ;
literal          = "null" | "true" | "false"
                 | integer-literal | real-literal | string-literal ;
```

Semicolons are mandatory where shown. Trailing commas are not accepted. Function
declarations are permitted only at the top level. There are no function
expressions or first-class function values.

An `else` associates with the immediately preceding unmatched `if`. Because each
`if` arm is a block, this rule matters only for the explicit `else if` form.

## 4. Values and types

Every runtime value has exactly one of these types:

| Type | Definition |
|---|---|
| `null` | The single value `null`. |
| `bool` | `true` or `false`. |
| `int` | A signed 64-bit two's-complement integer. |
| `real` | A finite C++ `double`. |
| `string` | An owned sequence of zero or more bytes. |

The language is dynamically typed: names and parameters do not have declared
types, and a mutable binding may hold values of different types over its
lifetime. Runtime operations still require their operands to have exactly the
types stated in this specification.

There are no implicit conversions. In particular:

- an `int` is not converted to a `real`;
- a value is not converted to `bool` for a condition;
- a value is not converted to `string` for concatenation; and
- `null` is not a missing, false, or zero value.

Version 0.1 has no language-level conversion operations. A host may register
explicit conversion functions if an application needs them.

Values have value semantics. Assigning or passing a string produces a logically
independent value; an implementation may share immutable storage internally as
long as this is not observable.

## 5. Names, bindings, and scope

### 5.1 Variable scopes

A program and each function invocation introduce lexical scopes. Each block
introduces a nested scope, except that a function's outer body is the function
invocation scope rather than an additional scope. Parameters and declarations
directly inside that body therefore share one scope. A name is resolved to the
nearest enclosing variable binding.

`let` creates an immutable binding and `var` creates a mutable binding. Both
require an initializer. The new binding is not visible within its own
initializer, so an initializer may refer to a shadowed outer binding:

```thimble
let size = 4;
{
    let size = size + 1; // reads the outer size
}
```

Two variables or parameters with the same name in one scope are a compile-time
name error. An inner scope may shadow a variable, parameter, or host value.

Assignment updates the nearest binding with the given name. Assignment to a
`let`, a parameter, or a host value is a compile-time name error. Parameters are
immutable in version 0.1. Assignment to an unknown name is also a compile-time
name error.

### 5.2 Program-scope bindings

All top-level `let` and `var` declarations define program-scope slots. These
slots are allocated uninitialized at the start of each execution, then initialized
when their declarations are reached in source order. A function may refer to any
program-scope variable regardless of the textual location of the function or
variable declaration.

Reading or assigning a program-scope slot before its declaration has completed
is a runtime name error. The following therefore compiles but fails when `read`
is called:

```thimble
fn read() { return answer; }
let first = read();
let answer = 42;
```

Program-scope state belongs to one execution and is discarded when that
execution returns or fails. It never persists implicitly between executions.

Function-local declarations are visible only within their lexical block and may
not be referenced by a separately declared function. Thimble has no closures.

### 5.3 Callable names

Script functions and host functions occupy one callable namespace, separate from
the variable namespace. Consequently, a variable and a function may have the
same spelling: `work` resolves a variable and `work()` resolves a callable.

Script function declarations are available throughout the entire program and
may call functions declared later. Mutual recursion is permitted. Two script
functions with the same name, or a script function with the same name as a host
function in the compilation interface, are compile-time name errors. Calling an
unknown function is a compile-time name error.

## 6. Statements and control flow

Statements execute in source order unless control flow says otherwise.

### 6.1 Blocks

A block executes its statements in order in a new lexical scope. Its bindings
cease to exist on normal exit, `return`, or error.

### 6.2 Expression statements

An expression statement evaluates its expression and discards the resulting
value. It is commonly used to invoke a function for its host-defined effects.

### 6.3 Conditions

The condition of `if` or `while` must evaluate to `bool`. Any other type causes a
runtime type error. There is no truthiness rule.

`if` evaluates its condition once. It executes exactly one selected arm, or no
arm when the condition is false and there is no `else`.

`while` evaluates its condition before every iteration. It stops when the
condition is false. Version 0.1 has no `break` or `continue` statement.

### 6.4 Return

`return expression;` evaluates the expression and returns that value.
`return;` returns `null`.

Inside a function, `return` ends the innermost function invocation. Reaching the
closing brace of a function without returning is equivalent to `return null;`.

At top level, `return` ends program execution and supplies the execution result.
Reaching end of source without returning produces `null`. A `return` inside a
top-level block or loop still returns from the program.

## 7. Expressions and operators

### 7.1 Evaluation order

Except for the short-circuit rules of `&&` and `||`, expression operands and
function arguments are evaluated exactly once from left to right. Parentheses
override precedence but do not otherwise change evaluation.

For a non-short-circuit binary operator, both operands are evaluated before the
operator's type requirements are checked. An error or host effect in the right
operand can therefore occur even when the left operand will ultimately make the
operation invalid.

Operator precedence, from highest to lowest, is:

1. primary expressions and calls;
2. unary `!` and unary `-`, right-associative;
3. `*`, `/`, and `%`, left-associative;
4. `+` and `-`, left-associative;
5. `<`, `<=`, `>`, and `>=`, left-associative;
6. `==` and `!=`, left-associative;
7. `&&`, left-associative; and
8. `||`, left-associative.

Assignment is a statement, not an expression.

### 7.2 Operator signatures

Operators accept only the following exact operand types:

| Operator | Operands | Result | Meaning |
|---|---|---|---|
| unary `-` | `int` | `int` | Checked arithmetic negation. |
| unary `-` | `real` | `real` | Arithmetic negation. |
| `!` | `bool` | `bool` | Boolean negation. |
| `+ - *` | `int`, `int` | `int` | Checked integer arithmetic. |
| `/` | `int`, `int` | `int` | Checked division, truncated toward zero. |
| `%` | `int`, `int` | `int` | Remainder corresponding to division truncated toward zero. |
| `+ - * /` | `real`, `real` | `real` | Floating-point arithmetic. |
| `+` | `string`, `string` | `string` | Byte-sequence concatenation. |
| `< <= > >=` | `int`, `int` | `bool` | Signed numeric comparison. |
| `< <= > >=` | `real`, `real` | `bool` | Floating-point numeric comparison. |
| `< <= > >=` | `string`, `string` | `bool` | Lexicographic comparison of unsigned bytes. |
| `== !=` | two values of the same type | `bool` | Value equality or inequality. |
| `&& ||` | `bool`, `bool` | `bool` | Short-circuit Boolean operation. |

`real % real` is not defined. Ordered comparison of `null` or `bool` is not
defined. Applying an operator to any unlisted type combination is a runtime type
error. Thus `1 + 2.0` and `1 == 1.0` are errors rather than conversions or false
comparisons.

Two `null` values are equal. Booleans and integers compare by value. Strings
compare byte-for-byte. Reals compare using ordinary finite `double` value
comparison; because non-finite values are forbidden, NaN behavior is not exposed.
Positive and negative real zero compare equal.

### 7.3 Short-circuit operations

For `a && b`, `a` is evaluated first and must be `bool`. If it is false, the
result is `false` and `b` is not evaluated. Otherwise `b` is evaluated, must be
`bool`, and becomes the result.

For `a || b`, `a` is evaluated first and must be `bool`. If it is true, the
result is `true` and `b` is not evaluated. Otherwise `b` is evaluated, must be
`bool`, and becomes the result.

### 7.4 Arithmetic failures

Integer arithmetic is checked. Overflow in negation, addition, subtraction,
multiplication, or the special division `INT64_MIN / -1` is a runtime error.
Integer division or remainder by zero is a runtime error. No arithmetic operation
has undefined C++ behavior.

Every real result shall be finite. Division by real positive or negative zero,
overflow to infinity, or production of NaN is a runtime error. Underflow to a
finite subnormal value or zero is permitted. Implementations use the host
`double` operations and are not required to make the least-significant bits
identical across different C++ platforms.

String concatenation fails with a runtime resource error if its required size is
not representable by the implementation or storage cannot be obtained.

## 8. Functions

A script function has a fixed arity equal to its number of parameters. Duplicate
parameter names are compile-time name errors. Because every call target is named
and its arity is part of the compilation interface, a call with the wrong number
of arguments is a compile-time type error.

Arguments are evaluated left to right before the function is entered. Their
values initialize immutable parameter bindings. Local variables and parameters
exist only for the duration of the call.

Functions may call themselves, other script functions, and registered host
functions. Functions are not values: they cannot be stored, passed as arguments,
returned, or constructed dynamically.

There are no default, optional, variadic, named, or overloaded script parameters.

## 9. Program lifecycle

### 9.1 Compilation

Compilation consumes:

1. source bytes and an optional source name; and
2. a host interface describing the available external value names and host
   callable names and arities.

Successful compilation produces an immutable `Program`. Compilation performs
lexing, parsing, lexical name resolution, mutability checks, duplicate checks,
and host-interface name resolution. It need not predict operand types, because
those types may vary by execution.

The compiled program shall not retain a borrowed view of source text unless the
public API explicitly transfers that lifetime responsibility to the caller.

### 9.2 Execution

Execution consumes:

1. a compiled `Program`;
2. a host context compatible with the compilation interface; and
3. execution limits.

Each execution creates fresh program-scope and call state, executes top-level
items in source order, and returns either one `Value` or one structured error.
Function declarations have no runtime action when encountered.

Host value bindings are read-only from the script. Their values are read from
the execution context, so a host may supply different values for separate
executions of the same program. Their types may also differ between executions;
ordinary runtime type rules then apply.

An execution observes a stable set of host values and callables. The host shall
not mutate an execution context concurrently with its execution. Whether host
values are copied eagerly or read through stable slots is an API choice, but any
host mutation during a callback shall not change values already visible to the
current execution.

### 9.3 Reuse and concurrency

A `Program` is immutable after successful compilation and may be executed more
than once. Separate executions do not share script variables.

An implementation should permit the same `Program` to execute concurrently when
each execution uses a distinct host context. The implementation is not required
to synchronize a shared mutable host context or callbacks supplied by the host.

Compiled representation, source locations, and immutable constants may be
shared among executions. This sharing must not change language behavior.

## 10. Host embedding contract

This section is normative at the behavioral level. Implementations may choose
different C++ names and class arrangements while preserving these capabilities.

### 10.1 C++ value mapping

The public API shall expose an owning tagged `Value` capable of representing all
Thimble values. It shall support these mappings:

| Thimble type | C++ representation |
|---|---|
| `null` | a dedicated null tag |
| `bool` | `bool` |
| `int` | `std::int64_t` |
| `real` | `double` |
| `string` | owning `std::string` or equivalent owning byte string |

Constructing or returning a non-finite `real` through the host API shall fail
with a structured host/value error; it shall never inject NaN or infinity into
script evaluation.

Accessors shall be checked. Requesting a C++ representation that does not match
the active tag shall report failure rather than coerce or produce undefined
behavior.

### 10.2 Host values

The host interface can declare named value slots. At execution time, the context
shall provide one valid `Value` for each slot that the program uses. From the
script, these slots behave as outermost immutable bindings and can be shadowed by
script variables.

A missing required slot or incompatible context is a host-interface error before
the first source statement executes.

### 10.3 Host functions

The host interface can register a uniquely named callable with a fixed arity.
When called, the callback receives:

- an ordered, read-only view of fully evaluated argument values; and
- optional execution/host state chosen by the embedding API.

It returns either a valid `Value` or a structured callback failure. A callback
failure becomes a runtime host error located at the call expression. Its safe
message and host-defined error code may be retained by the Thimble error.

Version 0.1 defines no built-in functions. Every callable name is supplied by a
script function declaration or by the host interface.

Host callbacks are responsible for validating their argument types. The API
shall provide checked helpers so that a mismatch can be reported as a Thimble
type error at the call site. Callbacks shall not retain borrowed argument views
after returning.

The interpreter cannot preempt a host callback. Long-running callbacks must
implement their own time, cancellation, or work limits. A callback may have
effects, and the order of those effects follows the evaluation order in Section
7.1.

If a C++ exception escapes a callback, an exception-enabled implementation shall
catch it at the Thimble boundary and convert it to a generic host error. The
exception text must not be exposed unless the host explicitly marks it safe.
An exception-free build may instead require callbacks to be `noexcept`. Thimble
itself has no `throw`, `try`, or catchable exception mechanism.

### 10.4 Interface compatibility

A compiled program records or otherwise validates the host interface it used for
name resolution. An execution context is compatible when every referenced value
slot still exists and every referenced host callable has the same name and
arity. Callback objects and bound values may change while preserving that shape.

Executing with an incompatible context shall fail before script execution. It
must not silently rebind a compiled reference to a different name or arity.

### 10.5 Minimum API operations

Without requiring exact C++ spelling, the public embedding API shall provide:

- creation of values and checked inspection of their tags and contents;
- construction of a host interface with value slots and fixed-arity callbacks;
- compilation from source plus host interface, returning `Program` or errors;
- execution of `Program` with a compatible context and limits, returning `Value`
  or an error; and
- inspection of every error field defined in Section 11.

Failure-bearing API operations shall use explicit result objects in the core
contract. They shall not require C++ exceptions for routine script errors.

## 11. Errors and diagnostics

### 11.1 Error phases and categories

Every failure has a phase and category.

Phases are:

- `compile`; and
- `execute`.

Required categories are:

| Category | Typical cases |
|---|---|
| `lexical` | invalid byte, escape, comment, or literal |
| `syntax` | token sequence does not match the grammar |
| `name` | unknown/redeclared name, illegal assignment, uninitialized root slot |
| `type` | wrong call arity or runtime operand, condition, or argument type |
| `runtime` | arithmetic, resource, host callback, or interface failure |
| `limit` | step or call-depth limit exceeded |

`lexical` and `syntax` errors occur only during compilation. An arity mismatch is
a compile-time `type` error; other `type` errors occur during execution. `limit`
errors occur only during execution. `name` and `runtime` may occur in either
phase as described elsewhere.

Each error shall contain at least:

- a stable machine-readable error code;
- phase and category;
- a human-readable message;
- a primary source span; and
- zero or more related source spans or call frames.

Messages are not stable API. Applications shall branch on codes or categories.

Stable codes shall include, at minimum, `unexpected_character`, `unterminated_string`,
`unexpected_token`, `unknown_name`, `duplicate_name`, `immutable_assignment`,
`uninitialized_binding`, `type_mismatch`, `arity_mismatch`, `integer_overflow`,
`division_by_zero`, `non_finite_real`, `host_failure`,
`incompatible_host_interface`, `step_limit`, and `call_depth_limit`.

### 11.2 Source locations

A source span is a half-open byte range `[begin, end)` and includes:

- the optional source name supplied at compilation;
- zero-based byte offsets;
- one-based start line and byte column; and
- one-based end line and byte column.

Columns count source bytes, not Unicode code points or display cells. The first
byte after a line break is column 1. A tab advances the column by one for this
machine-readable value; diagnostic renderers may visually expand it.

Runtime operator errors identify the operator token. Condition errors identify
the condition expression. Call and arity errors identify the call expression.
Errors originating in a host callback identify the script call site.
An error with no natural token, such as failure while validating an execution
context, identifies the first source reference that caused validation or uses an
empty span at byte offset zero when no such reference exists.

### 11.3 Call traces

An execution error shall include a call trace from the innermost active callable
outward. A frame contains the script or host callable name and the source span of
its call site when one exists. The top-level program may appear as a synthetic
final frame.

Trace construction must not itself execute script code or invoke callbacks.

### 11.4 Reporting policy

An implementation may stop compilation after the first error or return multiple
compile errors. If it returns multiple errors, their ordering shall follow source
order and speculative recovery diagnostics should be clearly marked. Execution
always stops at the first error.

## 12. Execution limits and resource safety

Every execution receives a non-negative step limit and non-negative call-depth
limit. The host API may provide defaults, but it shall also permit the caller to
set both. A value of zero is not “unlimited”; it permits no chargeable work. An
implementation may expose an explicit trusted-mode unlimited option separately.

### 12.1 Step accounting

The following each consume one step immediately before they occur:

- entry into any statement and into any block, including a function body or a
  block used as an `if` or `while` body;
- evaluation of any expression node, including literals and name reads;
- execution of an assignment after its right-hand side is evaluated;
- initialization of a declared binding after its initializer is evaluated; and
- invocation of a script or host callable after its arguments are evaluated.

The `while` statement itself is entered once; its condition expression and body
block are evaluated and charged again for each iteration. Short-circuited
expressions and untaken statements consume no steps. A function declaration
consumes no execution step when passed at top level. Internal
implementation operations such as bytecode dispatch do not alter the normative
count.

Before a chargeable action, if no step remains, execution stops with `step_limit`
at the source span of that action. The action is not performed. A callback counts
only for its invocation; work inside it is outside Thimble step accounting.

At a script call, argument errors occur first, followed by the invocation's step
check, followed by its call-depth check. Host-interface compatibility is checked
before any source action or step charge. Otherwise, left-to-right evaluation and
the first error encountered determine which execution error is reported.

The explicit accounting model makes a given program and host-call result consume
the same number of Thimble steps regardless of interpreter implementation.

### 12.2 Call depth

Call depth is the number of active script-function invocations. Top-level
execution has depth zero. Before entering a script function, the implementation
checks whether the resulting depth would exceed the configured maximum. If so,
execution stops with `call_depth_limit` at the call site and the function body is
not entered.

A host callback does not add script call depth. If an embedding API permits a
callback to initiate another Thimble execution, that execution has independent
limits and state; such re-entry is not part of the current execution's depth.

### 12.3 Memory and host resources

Version 0.1 does not define a portable hard memory limit. An implementation shall
detect representational size overflow and convert recoverable allocation failure
to a structured runtime resource error where the C++ environment permits.
Embedders handling untrusted input should additionally cap source size, bound
host callback work, and use an implementation-provided allocation limit if one
is available.

The absence of objects, containers, closures, and first-class functions means all
script-managed lifetimes are bounded by an execution or owned scalar/string
value. No tracing garbage collector is required.

## 13. Determinism and observable effects

Given identical source, host interface, execution values, callback return values,
callback effects, limits, and compatible `double` behavior, Thimble evaluates in
a deterministic order.

The following are deliberately platform-dependent:

- the least-significant behavior of finite `double` arithmetic permitted by C++;
- success or failure caused by available memory; and
- host callback behavior.

No unspecified operand or argument evaluation order is exposed to the script.

## 14. Exclusions in version 0.1

The following are not part of the language:

- arrays, maps, tuples, objects, records, classes, and user-defined types;
- first-class functions, lambdas, closures, and nested function declarations;
- modules, imports, includes, and separate script compilation units;
- language exceptions, cleanup handlers, and deferred execution;
- `break`, `continue`, `for`, pattern matching, and switch statements;
- implicit or built-in explicit type conversion;
- operator overloading;
- concurrency, coroutines, asynchronous functions, and shared script state;
- file, network, process, clock, environment, console, or random access unless a
  host explicitly supplies it through a callback;
- reflection and dynamic evaluation;
- persistent globals between executions; and
- garbage collection.

An implementation may expose non-language tooling such as disassembly, tracing,
profiling, or diagnostic formatting. Such extensions must not change execution
semantics unless enabled through a separately identified language version.

## 15. Examples

### 15.1 Basic program

```thimble
fn clamp(value, low, high) {
    if (value < low) {
        return low;
    } else if (value > high) {
        return high;
    }
    return value;
}

let requested = input_value; // immutable host value
var adjusted = clamp(requested, 0, 100);

while (adjusted < 50) {
    adjusted = adjusted + 10;
}

return adjusted;
```

If `input_value` is an `int`, this program returns an `int`. If it is a `real`,
the call fails when comparison with the `int` bounds is attempted; Thimble does
not promote the bounds to `real`.

### 15.2 Host function

Given a host callable `emit` of arity one:

```thimble
fn greet(name) {
    emit("Hello, " + name);
}

greet(user_name);
return null;
```

The host decides what `emit` does. The language itself performs no output.

### 15.3 Dynamic mutable binding

```thimble
var value = 10;
value = "ten"; // valid: mutability applies to the binding, not its current type
return value;
```

### 15.4 Exact-type error

```thimble
return 1 + 2.0;
```

This compiles and produces a runtime `type_mismatch` located at `+`.

## 16. Versioning

This document specifies language version `0.1`. A compiled program shall record
the language version and relevant opt-in feature flags. An implementation must
not silently reinterpret a compiled program under incompatible semantics.

Additions that make previously invalid source valid may be introduced in a later
minor version. Any change to the meaning, evaluation order, type, result, error
category, or step cost of valid source requires an explicitly selected language
version.
