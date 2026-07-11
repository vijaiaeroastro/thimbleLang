#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace thimble {
/// Broad class of an error. Compile-time and execution-time failures use the
/// same object so an embedding application can report them uniformly.
enum class ErrorCategory { lexical, syntax, name, type, runtime, limit, host };
/// Half-open byte range in the original source, with one-based line/column.
struct Span { std::size_t begin=0,end=0,line=1,column=1; };
/// Structured diagnostic returned by compilation and execution.
struct Error {
  ErrorCategory category=ErrorCategory::runtime;
  std::string code, message, source;
  Span span;
  std::vector<std::string> trace;
};
/// A small exception-free result type used by the public API.
template<class T> class Result {
  bool ok_; T value_{}; Error error_{};
public:
  Result(T v):ok_(true),value_(std::move(v)){} Result(Error e):ok_(false),error_(std::move(e)){}
  /// True when the operation produced a value.
  bool ok() const { return ok_; } explicit operator bool() const { return ok_; }
  /// Access the value. Call only after checking `ok()`.
  T& value(){return value_;} const T& value() const{return value_;}
  /// Access the diagnostic. Call only after a failed result.
  const Error& error() const{return error_;}
};
/// Construct a diagnostic with an optional source location and source name.
inline Error make_error(ErrorCategory c,std::string code,std::string msg,Span s={},std::string source={}) {
  return Error{c,std::move(code),std::move(msg),std::move(source),s,{}};
}
}
