#pragma once
#include "ast.hpp"
#include "lexer.hpp"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

namespace thimble {

/// Recursive-descent parser for the grammar in SPEC.md.
class Parser {
  std::vector<Token> tokens_;
  std::size_t position_ = 0;
  Error error_{};
  bool failed_ = false;

  const Token &current() const { return tokens_[position_]; }
  const Token &previous() const { return tokens_[position_ - 1]; }
  bool is(Tok kind) const { return current().kind == kind; }

  Token take() { return tokens_[position_++]; }

  bool eat(Tok kind) {
    if (!is(kind))
      return false;
    ++position_;
    return true;
  }

  void fail(std::string code, std::string message) {
    if (failed_)
      return;
    failed_ = true;
    error_ = make_error(ErrorCategory::syntax, std::move(code),
                        std::move(message), current().span);
  }

  bool need(Tok kind, const char *message) {
    if (eat(kind))
      return true;
    fail("unexpected_token", message);
    return false;
  }

  static Span through(Span first, Span last) {
    if (!first.valid())
      return last;
    first.end = last.end;
    first.end_line = last.end_line;
    first.end_column = last.end_column;
    return first;
  }

  std::unique_ptr<Expr> expression() { return logical_or(); }

  std::unique_ptr<Expr> logical_or() {
    auto value = logical_and();
    while (!failed_ && is(Tok::or_)) {
      auto operation = take();
      value = binary(Binary::or_, operation, std::move(value), logical_and());
    }
    return value;
  }

  std::unique_ptr<Expr> logical_and() {
    auto value = equality();
    while (!failed_ && is(Tok::and_)) {
      auto operation = take();
      value = binary(Binary::and_, operation, std::move(value), equality());
    }
    return value;
  }

  std::unique_ptr<Expr> equality() {
    auto value = comparison();
    while (!failed_ && (is(Tok::eq) || is(Tok::ne))) {
      auto operation = take();
      value = binary(operation.kind == Tok::eq ? Binary::eq : Binary::ne,
                     operation, std::move(value), comparison());
    }
    return value;
  }

  std::unique_ptr<Expr> comparison() {
    auto value = term();
    while (!failed_ &&
           (is(Tok::lt) || is(Tok::le) || is(Tok::gt) || is(Tok::ge))) {
      auto operation = take();
      Binary kind = Binary::ge;
      if (operation.kind == Tok::lt)
        kind = Binary::lt;
      else if (operation.kind == Tok::le)
        kind = Binary::le;
      else if (operation.kind == Tok::gt)
        kind = Binary::gt;
      value = binary(kind, operation, std::move(value), term());
    }
    return value;
  }

  std::unique_ptr<Expr> term() {
    auto value = factor();
    while (!failed_ && (is(Tok::plus) || is(Tok::minus))) {
      auto operation = take();
      value = binary(operation.kind == Tok::plus ? Binary::add : Binary::sub,
                     operation, std::move(value), factor());
    }
    return value;
  }

  std::unique_ptr<Expr> factor() {
    auto value = unary();
    while (!failed_ && (is(Tok::star) || is(Tok::slash) || is(Tok::percent))) {
      auto operation = take();
      Binary kind = Binary::mod;
      if (operation.kind == Tok::star)
        kind = Binary::mul;
      else if (operation.kind == Tok::slash)
        kind = Binary::div;
      value = binary(kind, operation, std::move(value), unary());
    }
    return value;
  }

  std::unique_ptr<Expr> unary() {
    if (!is(Tok::minus) && !is(Tok::not_))
      return primary();
    auto operation = take();
    auto value = std::make_unique<Expr>();
    value->kind = Expr::Kind::unary;
    value->operator_span = operation.span;
    value->unary = operation.kind == Tok::minus ? Unary::neg : Unary::not_;
    value->left = unary();
    value->span = through(operation.span, value->left->span);
    return value;
  }

  std::unique_ptr<Expr> atom() {
    if (is(Tok::int_lit) || is(Tok::real_lit) || is(Tok::str_lit) ||
        is(Tok::kw_true) || is(Tok::kw_false) || is(Tok::kw_null)) {
      auto token = take();
      auto value = std::make_unique<Expr>();
      value->kind = Expr::Kind::literal;
      value->span = token.span;
      if (token.kind == Tok::str_lit)
        value->literal = Value(token.text);
      else if (token.kind == Tok::kw_true)
        value->literal = Value(true);
      else if (token.kind == Tok::kw_false)
        value->literal = Value(false);
      else if (token.kind == Tok::kw_null)
        value->literal = Value();
      else if (token.kind == Tok::int_lit) {
        errno = 0;
        auto number = std::strtoll(token.text.c_str(), nullptr, 10);
        if (errno || number < 0)
          fail("invalid_literal", "integer literal out of range");
        else
          value->literal = Value(static_cast<std::int64_t>(number));
      } else {
        errno = 0;
        char *end = nullptr;
        auto number = std::strtod(token.text.c_str(), &end);
        if (errno || !end || *end || !std::isfinite(number))
          fail("invalid_literal", "real literal out of range");
        else
          value->literal = Value(number);
      }
      return value;
    }

    if (is(Tok::id)) {
      auto token = take();
      auto value = std::make_unique<Expr>();
      value->kind = Expr::Kind::name;
      value->span = token.span;
      value->name = token.text;
      return value;
    }

    if (is(Tok::lparen)) {
      auto opening = take();
      auto value = expression();
      if (need(Tok::rparen, "expected ')' after expression"))
        value->span = through(opening.span, previous().span);
      return value;
    }

    if (is(Tok::lbracket)) {
      auto opening = take();
      auto value = std::make_unique<Expr>();
      value->kind = Expr::Kind::array;
      value->span = opening.span;
      if (!is(Tok::rbracket)) {
        do {
          value->elements.push_back(expression());
        } while (eat(Tok::comma));
      }
      if (need(Tok::rbracket, "expected ']' after array"))
        value->span = through(opening.span, previous().span);
      return value;
    }

    if (is(Tok::lbrace)) {
      auto opening = take();
      auto value = std::make_unique<Expr>();
      value->kind = Expr::Kind::map;
      value->span = opening.span;
      if (!is(Tok::rbrace)) {
        do {
          auto key = expression();
          need(Tok::colon, "expected ':' after map key");
          auto mapped = expression();
          value->entries.emplace_back(std::move(key), std::move(mapped));
        } while (eat(Tok::comma));
      }
      if (need(Tok::rbrace, "expected '}' after map"))
        value->span = through(opening.span, previous().span);
      return value;
    }

    fail("unexpected_token", "expected expression");
    return std::make_unique<Expr>();
  }

  std::unique_ptr<Expr> primary() {
    auto value = atom();
    while (!failed_) {
      if (eat(Tok::lparen)) {
        auto call = std::make_unique<Expr>();
        call->kind = Expr::Kind::call;
        call->span = value->span;
        if (value->kind == Expr::Kind::name)
          call->name = value->name;
        else
          call->left = std::move(value);
        if (!is(Tok::rparen)) {
          do {
            call->args.push_back(expression());
          } while (eat(Tok::comma));
        }
        if (need(Tok::rparen, "expected ')' after arguments"))
          call->span = through(call->span, previous().span);
        value = std::move(call);
        continue;
      }
      if (eat(Tok::lbracket)) {
        auto index = std::make_unique<Expr>();
        index->kind = Expr::Kind::index;
        index->span = value->span;
        index->left = std::move(value);
        index->right = expression();
        if (need(Tok::rbracket, "expected ']' after index"))
          index->span = through(index->span, previous().span);
        value = std::move(index);
        continue;
      }
      if (eat(Tok::dot)) {
        if (!is(Tok::id)) {
          fail("expected_name", "expected member name");
          return value;
        }
        auto member_name = take();
        auto member = std::make_unique<Expr>();
        member->kind = Expr::Kind::member;
        member->span = through(value->span, member_name.span);
        member->left = std::move(value);
        member->name = member_name.text;
        value = std::move(member);
        continue;
      }
      break;
    }
    return value;
  }

  std::unique_ptr<Expr> binary(Binary kind, const Token &operation,
                               std::unique_ptr<Expr> left,
                               std::unique_ptr<Expr> right) {
    auto value = std::make_unique<Expr>();
    value->kind = Expr::Kind::binary;
    value->span = through(left->span, right->span);
    value->operator_span = operation.span;
    value->binary = kind;
    value->left = std::move(left);
    value->right = std::move(right);
    return value;
  }

  std::unique_ptr<Stmt> statement() {
    if (is(Tok::kw_let) || is(Tok::kw_var)) {
      auto keyword = take();
      if (!is(Tok::id)) {
        fail("expected_name", "expected variable name");
        return {};
      }
      auto name = take();
      auto result = std::make_unique<Stmt>();
      result->kind = Stmt::Kind::var;
      result->span = keyword.span;
      result->name = name.text;
      result->mutable_ = keyword.kind == Tok::kw_var;
      need(Tok::assign, "expected '='");
      result->expr = expression();
      if (need(Tok::semi, "expected ';'"))
        result->span = through(keyword.span, previous().span);
      return result;
    }

    if (is(Tok::kw_if)) {
      auto keyword = take();
      auto result = std::make_unique<Stmt>();
      result->kind = Stmt::Kind::if_;
      result->span = keyword.span;
      need(Tok::lparen, "expected '('");
      result->condition = expression();
      need(Tok::rparen, "expected ')'");
      result->then_branch = block();
      result->span = through(keyword.span, result->then_branch->span);
      if (eat(Tok::kw_else)) {
        result->else_branch = is(Tok::kw_if) ? statement() : block();
        result->span = through(keyword.span, result->else_branch->span);
      }
      return result;
    }

    if (is(Tok::kw_while)) {
      auto keyword = take();
      auto result = std::make_unique<Stmt>();
      result->kind = Stmt::Kind::while_;
      need(Tok::lparen, "expected '('");
      result->condition = expression();
      need(Tok::rparen, "expected ')'");
      result->then_branch = block();
      result->span = through(keyword.span, result->then_branch->span);
      return result;
    }

    if (is(Tok::kw_return)) {
      auto keyword = take();
      auto result = std::make_unique<Stmt>();
      result->kind = Stmt::Kind::ret;
      result->span = keyword.span;
      if (!is(Tok::semi))
        result->expr = expression();
      if (need(Tok::semi, "expected ';'"))
        result->span = through(keyword.span, previous().span);
      return result;
    }

    if (is(Tok::lbrace))
      return block();

    auto left = expression();
    auto result = std::make_unique<Stmt>();
    result->span = left->span;
    if (eat(Tok::assign)) {
      result->kind = Stmt::Kind::assign;
      result->target = std::move(left);
      result->expr = expression();
    } else {
      result->kind = Stmt::Kind::expr;
      result->expr = std::move(left);
    }
    if (need(Tok::semi, "expected ';'"))
      result->span = through(result->span, previous().span);
    return result;
  }

  std::unique_ptr<Stmt> block() {
    auto opening = current();
    auto result = std::make_unique<Stmt>();
    result->kind = Stmt::Kind::block;
    result->span = opening.span;
    need(Tok::lbrace, "expected '{'");
    while (!failed_ && !is(Tok::rbrace) && !is(Tok::end))
      result->body.push_back(statement());
    if (need(Tok::rbrace, "expected '}'"))
      result->span = through(opening.span, previous().span);
    return result;
  }

public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  /// Parse top-level statements and function declarations.
  Result<std::pair<std::vector<std::unique_ptr<Stmt>>, std::vector<Function>>>
  parse() {
    std::vector<std::unique_ptr<Stmt>> statements;
    std::vector<Function> functions;
    while (!failed_ && !is(Tok::end)) {
      if (!eat(Tok::kw_fn)) {
        statements.push_back(statement());
        continue;
      }
      auto opening = previous();
      Function function;
      function.span = opening.span;
      if (!is(Tok::id)) {
        fail("expected_name", "expected function name");
        break;
      }
      function.name = take().text;
      need(Tok::lparen, "expected '('");
      if (!is(Tok::rparen)) {
        do {
          if (!is(Tok::id)) {
            fail("expected_name", "expected parameter name");
            break;
          }
          function.params.push_back(take().text);
        } while (eat(Tok::comma));
      }
      need(Tok::rparen, "expected ')'");
      function.body = block();
      function.span = through(opening.span, function.body->span);
      functions.push_back(std::move(function));
    }
    if (failed_)
      return error_;
    return std::make_pair(std::move(statements), std::move(functions));
  }
};

} // namespace thimble
