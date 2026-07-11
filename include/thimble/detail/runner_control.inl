Error err(ErrorCategory c, std::string code, std::string msg, Span s) {
  auto e = make_error(c, std::move(code), std::move(msg), s, p.source_,
                      p.source_name_);
  return e;
}
Error locate(Error error, Span span) {
  if (!error.span.valid()) {
    error.span = span;
    error.source = p.source_;
    error.source_name = p.source_name_;
  }
  return error;
}
Error host_error(Error error, Span span, std::string function) {
  error = locate(std::move(error), span);
  error.trace.push_back({std::move(function), p.source_name_, span});
  return error;
}
// Keep the counter in the runner, so every execution has independent limits
// even when the same immutable Program is run concurrently.
bool check_control(Span s) {
  if (lim.cancelled) {
    try {
      if (lim.cancelled()) {
        last = err(ErrorCategory::limit, "cancelled", "execution cancelled", s);
        return false;
      }
    } catch (...) {
      last = err(ErrorCategory::host, "host_failure",
                 "cancellation callback threw an exception", s);
      return false;
    }
  }
  if (lim.time_limit.count() > 0 &&
      std::chrono::steady_clock::now() - started >= lim.time_limit) {
    last = err(ErrorCategory::limit, "time_limit",
               "execution time limit exceeded", s);
    return false;
  }
  return true;
}
bool charge(Span s) {
  if (!check_control(s))
    return false;
  if (steps >= lim.steps) {
    last = err(ErrorCategory::limit, "step_limit",
               "execution step limit exceeded", s);
    return false;
  }
  ++steps;
  return true;
}
Error last{};
bool reserve_memory(std::size_t bytes, Span s) {
  if (bytes > lim.allocation_budget ||
      allocation_charged > lim.allocation_budget - bytes) {
    last = err(ErrorCategory::limit, "allocation_limit",
               "execution allocation budget exceeded", s);
    return false;
  }
  allocation_charged += bytes;
  return true;
}
