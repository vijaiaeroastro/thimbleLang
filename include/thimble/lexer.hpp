#pragma once
#include "error.hpp"
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace thimble {
/// Token kinds produced by the source lexer.
enum class Tok {
  end,
  id,
  int_lit,
  real_lit,
  str_lit,
  lparen,
  rparen,
  lbrace,
  rbrace,
  lbracket,
  rbracket,
  colon,
  dot,
  comma,
  semi,
  plus,
  minus,
  star,
  slash,
  percent,
  assign,
  eq,
  ne,
  lt,
  le,
  gt,
  ge,
  not_,
  and_,
  or_,
  kw_let,
  kw_var,
  kw_fn,
  kw_if,
  kw_else,
  kw_while,
  kw_return,
  kw_true,
  kw_false,
  kw_null
};
/// A token and its location in the source buffer.
struct Token {
  Tok kind;
  std::string text;
  Span span;
};
/// Converts source bytes into tokens without throwing C++ exceptions.
class Lexer {
  std::string src_;
  std::size_t p_ = 0, line_ = 1, col_ = 1;
  std::vector<Token> out_;
  Error error_{};
  bool failed_ = false;
  // `peek` never advances. `take` is the single place which updates location
  // counters, keeping diagnostics correct for comments and escaped strings.
  char peek(std::size_t n = 0) const {
    return p_ + n < src_.size() ? src_[p_ + n] : '\0';
  }
  char take() {
    char c = peek();
    if (!c)
      return c;
    ++p_;
    if (c == '\n') {
      ++line_;
      col_ = 1;
    } else
      ++col_;
    return c;
  }
  void fail(std::string c, std::string m, std::size_t b, std::size_t l,
            std::size_t co) {
    if (!failed_) {
      failed_ = true;
      auto end = p_;
      auto end_line = line_;
      auto end_column = col_;
      if (end == b && b < src_.size()) {
        end = b + 1;
        end_line = l;
        end_column = co + 1;
      }
      error_ = make_error(ErrorCategory::lexical, std::move(c), std::move(m),
                          Span{b, end, l, co, end_line, end_column});
    }
  }
  void add(Tok k, std::string t, std::size_t b, std::size_t l, std::size_t c) {
    out_.push_back({k, std::move(t), Span{b, p_, l, c, line_, col_}});
  }

public:
  explicit Lexer(std::string s) : src_(std::move(s)) {}
  /// Lex the complete source, returning the first lexical diagnostic on error.
  Result<std::vector<Token>> run() {
    while (p_ < src_.size() && !failed_) {
      char c = peek();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        take();
        continue;
      }
      if (c == '/' && peek(1) == '/') {
        while (peek() && peek() != '\n')
          take();
        continue;
      }
      if (c == '/' && peek(1) == '*') {
        auto b = p_;
        auto l = line_, co = col_;
        take();
        take();
        bool ok = false;
        while (peek()) {
          if (peek() == '*' && peek(1) == '/') {
            take();
            take();
            ok = true;
            break;
          }
          take();
        }
        if (!ok)
          fail("unterminated_comment", "unterminated block comment", b, l, co);
        continue;
      }
      auto b = p_, l = line_, co = col_;
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
        std::string t;
        while ((std::isalnum((unsigned char)peek()) || peek() == '_'))
          t += take();
        Tok k = Tok::id;
        if (t == "let")
          k = Tok::kw_let;
        else if (t == "var")
          k = Tok::kw_var;
        else if (t == "fn")
          k = Tok::kw_fn;
        else if (t == "if")
          k = Tok::kw_if;
        else if (t == "else")
          k = Tok::kw_else;
        else if (t == "while")
          k = Tok::kw_while;
        else if (t == "return")
          k = Tok::kw_return;
        else if (t == "true")
          k = Tok::kw_true;
        else if (t == "false")
          k = Tok::kw_false;
        else if (t == "null")
          k = Tok::kw_null;
        add(k, t, b, l, co);
        continue;
      }
      if (c >= '0' && c <= '9') {
        std::string t;
        while (std::isdigit((unsigned char)peek()))
          t += take();
        bool real = false;
        if (peek() == '.') {
          real = true;
          t += take();
          while (std::isdigit((unsigned char)peek()))
            t += take();
        }
        if (peek() == 'e' || peek() == 'E') {
          real = true;
          t += take();
          if (peek() == '+' || peek() == '-')
            t += take();
          if (!std::isdigit((unsigned char)peek())) {
            fail("invalid_literal", "invalid exponent", b, l, co);
            continue;
          }
          while (std::isdigit((unsigned char)peek()))
            t += take();
        }
        add(real ? Tok::real_lit : Tok::int_lit, t, b, l, co);
        continue;
      }
      if (c == '"') {
        take();
        std::string t;
        bool ok = false;
        while (peek()) {
          char x = take();
          if (x == '"') {
            ok = true;
            break;
          }
          if (x == '\n' || x == '\r' || x == '\0') {
            fail("unterminated_string", "unterminated string", b, l, co);
            break;
          }
          if (x != '\\') {
            t += x;
            continue;
          }
          char e = take();
          switch (e) {
          case '"':
            t += '"';
            break;
          case '\\':
            t += '\\';
            break;
          case 'n':
            t += '\n';
            break;
          case 'r':
            t += '\r';
            break;
          case 't':
            t += '\t';
            break;
          case '0':
            t += '\0';
            break;
          case 'x': {
            int v = 0;
            for (int i = 0; i < 2; i++) {
              char h = take();
              if (!std::isxdigit((unsigned char)h)) {
                fail("invalid_escape", "invalid hex escape", b, l, co);
                break;
              }
              v = v * 16 +
                  (h >= '0' && h <= '9'
                       ? h - '0'
                       : (h >= 'a' && h <= 'f' ? h - 'a' + 10 : h - 'A' + 10));
            }
            t.push_back(char(v));
            break;
          }
          default:
            fail("invalid_escape", "invalid string escape", b, l, co);
          }
        }
        if (!ok && !failed_)
          fail("unterminated_string", "unterminated string", b, l, co);
        if (!failed_)
          add(Tok::str_lit, t, b, l, co);
        continue;
      }
      Tok k = Tok::end;
      std::string t(1, c);
      switch (c) {
      case '(':
        k = Tok::lparen;
        break;
      case ')':
        k = Tok::rparen;
        break;
      case '{':
        k = Tok::lbrace;
        break;
      case '}':
        k = Tok::rbrace;
        break;
      case '[':
        k = Tok::lbracket;
        break;
      case ']':
        k = Tok::rbracket;
        break;
      case ':':
        k = Tok::colon;
        break;
      case '.':
        k = Tok::dot;
        break;
      case ',':
        k = Tok::comma;
        break;
      case ';':
        k = Tok::semi;
        break;
      case '+':
        k = Tok::plus;
        break;
      case '-':
        k = Tok::minus;
        break;
      case '*':
        k = Tok::star;
        break;
      case '%':
        k = Tok::percent;
        break;
      case '!':
        if (peek(1) == '=') {
          take();
          t = "!=";
          k = Tok::ne;
        } else
          k = Tok::not_;
        break;
      case '=':
        if (peek(1) == '=') {
          take();
          t = "==";
          k = Tok::eq;
        } else
          k = Tok::assign;
        break;
      case '<':
        if (peek(1) == '=') {
          take();
          t = "<=";
          k = Tok::le;
        } else
          k = Tok::lt;
        break;
      case '>':
        if (peek(1) == '=') {
          take();
          t = ">=";
          k = Tok::ge;
        } else
          k = Tok::gt;
        break;
      case '&':
        if (peek(1) == '&') {
          take();
          t = "&&";
          k = Tok::and_;
        }
        break;
      case '|':
        if (peek(1) == '|') {
          take();
          t = "||";
          k = Tok::or_;
        }
        break;
      case '/':
        k = Tok::slash;
        break;
      }
      if (k == Tok::end) {
        fail("unexpected_character", "unexpected character", b, l, co);
        continue;
      }
      take();
      add(k, t, b, l, co);
    }
    out_.push_back({Tok::end, "", Span{p_, p_, line_, col_, line_, col_}});
    if (failed_)
      return error_;
    return out_;
  }
};
} // namespace thimble
