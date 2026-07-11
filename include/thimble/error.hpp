#pragma once
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace thimble {
/// Broad class of an error. Compile-time and execution-time failures use the
/// same object so an embedding application can report them uniformly.
enum class ErrorCategory { lexical, syntax, name, type, runtime, limit, host };
/// Half-open byte range in the original source, with one-based line/column.
struct Span {
  std::size_t begin = 0, end = 0, line = 0, column = 0;
  std::size_t end_line = 0, end_column = 0;
  /// True when this span identifies source text.
  bool valid() const { return line != 0; }
};
/// One script or host call site retained while an error unwinds.
struct CallFrame {
  std::string function;
  std::string source_name;
  Span span;
};
/// Structured diagnostic returned by compilation and execution.
struct Error {
  ErrorCategory category = ErrorCategory::runtime;
  std::string code, message, source, source_name;
  Span span;
  std::vector<CallFrame> trace;
};
/// A small exception-free result type used by the public API.
template <class T> class Result {
  bool ok_;
  T value_{};
  Error error_{};

public:
  using value_type = T;
  Result(T v) : ok_(true), value_(std::move(v)) {}
  Result(Error e) : ok_(false), error_(std::move(e)) {}
  /// True when the operation produced a value.
  bool ok() const { return ok_; }
  explicit operator bool() const { return ok_; }
  /// Access the value. Call only after checking `ok()`.
  T &value() { return value_; }
  const T &value() const { return value_; }
  /// Access the diagnostic. Call only after a failed result.
  const Error &error() const { return error_; }
  Error &error() { return error_; }
};
/// Construct a diagnostic with an optional source location and source name.
inline Error make_error(ErrorCategory c, std::string code, std::string msg,
                        Span s = {}, std::string source = {},
                        std::string source_name = {}) {
  return Error{c,
               std::move(code),
               std::move(msg),
               std::move(source),
               std::move(source_name),
               s,
               {}};
}

inline const char *category_name(ErrorCategory category) {
  switch (category) {
  case ErrorCategory::lexical:
    return "lexical";
  case ErrorCategory::syntax:
    return "syntax";
  case ErrorCategory::name:
    return "name";
  case ErrorCategory::type:
    return "type";
  case ErrorCategory::runtime:
    return "runtime";
  case ErrorCategory::limit:
    return "limit";
  case ErrorCategory::host:
    return "host";
  }
  return "unknown";
}

/// Render a diagnostic with its source line, caret and call trace.
inline std::string format_error(const Error &error) {
  std::ostringstream output;
  output << category_name(error.category) << " error [" << error.code
         << "]: " << error.message;
  if (error.span.valid()) {
    output << "\n  at ";
    if (!error.source_name.empty())
      output << error.source_name << ':';
    output << error.span.line << ':' << error.span.column;
  }
  if (error.span.valid() && !error.source.empty()) {
    std::size_t line_start = 0;
    for (std::size_t line = 1;
         line < error.span.line && line_start < error.source.size(); ++line) {
      line_start = error.source.find('\n', line_start);
      if (line_start == std::string::npos)
        break;
      ++line_start;
    }
    if (line_start != std::string::npos && line_start < error.source.size()) {
      auto line_end = error.source.find_first_of("\r\n", line_start);
      if (line_end == std::string::npos)
        line_end = error.source.size();
      output << "\n\n"
             << error.span.line << " | "
             << error.source.substr(line_start, line_end - line_start)
             << "\n  | "
             << std::string(error.span.column > 0 ? error.span.column - 1 : 0,
                            ' ')
             << '^';
    }
  }
  for (const auto &frame : error.trace) {
    output << "\n  called from " << frame.function;
    if (!frame.source_name.empty() && frame.span.valid())
      output << " at " << frame.source_name << ':' << frame.span.line << ':'
             << frame.span.column;
    else if (frame.span.valid())
      output << " at " << frame.span.line << ':' << frame.span.column;
  }
  return output.str();
}
} // namespace thimble
