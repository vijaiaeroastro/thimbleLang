#include "thimble/thimble.hpp"

#include <string>
#include <vector>

int main() {
  const std::vector<std::string> seeds = {
      "return 1;", "fn f(x) { return x + 1; } return f(2);",
      "var a = [1, 2, 3]; a[0] = 4; return a[0];",
      "return {\"name\": \"value\"};",
      "if (true) { return null; } else { return false; }"};

  for (const auto &seed : seeds) {
    for (std::size_t position = 0; position <= seed.size(); ++position) {
      for (char replacement : {'\0', '@', '{', '}', '[', ']', '"', ';'}) {
        auto mutated = seed;
        if (position == mutated.size()) {
          mutated.push_back(replacement);
        } else {
          mutated[position] = replacement;
        }
        (void)thimble::compile(std::move(mutated));
      }
    }
  }
}
