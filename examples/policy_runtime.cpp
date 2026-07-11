#include "thimble/thimble.hpp"
#include <iostream>
#include <memory>
#include <string>

struct Session {
  std::string role = "editor";
  bool enabled = true;
  int attempts = 1;
};

int score(int base, int weight) { return base * weight; }

int main() {
  thimble::HostContext host;
  auto session = std::make_shared<Session>();
  auto session_type = host.define_object_type<Session>("Session");
  session_type.property("role", &Session::role)
      .property("enabled", &Session::enabled)
      .property("attempts", &Session::attempts);
  host.bind_object("session", session, session_type);
  host.bind_value("minimum_score", 10);
  host.bind_function("score", score);

  const std::string source = R"(
    fn may_edit() {
      return session.enabled &&
             (session.role == "editor" || session.role == "admin");
    }

    let valid = session.attempts >= 0 && session.attempts <= 5;
    let visible = may_edit() && score(session.attempts, 6) >= minimum_score;
    return {"valid": valid, "visible": visible, "role": session.role};
  )";

  auto program = thimble::compile(source, "policy.thimble", host);
  if (!program) {
    std::cerr << thimble::format_error(program.error()) << '\n';
    return 1;
  }
  auto result = program.value().execute(host);
  if (!result) {
    std::cerr << thimble::format_error(result.error()) << '\n';
    return 1;
  }

  const auto entries = result.value().as_map().value()->entries;
  for (const auto &entry : entries) {
    std::cout << entry.first.as_string().value() << " = ";
    if (entry.second.type() == thimble::Type::boolean)
      std::cout << (entry.second.as_bool().value() ? "true" : "false");
    else
      std::cout << entry.second.as_string().value();
    std::cout << '\n';
  }
}
