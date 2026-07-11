#pragma once
#include "value.hpp"
#include <memory>
#include <string>
#include <vector>

namespace thimble {
// These nodes are compiler-owned. unique_ptr makes the tree ownership explicit
// and keeps a Program movable without copying script nodes.
enum class Binary {
  add,
  sub,
  mul,
  div,
  mod,
  eq,
  ne,
  lt,
  le,
  gt,
  ge,
  and_,
  or_
};
enum class Unary { neg, not_ };
struct Expr {
  enum class Kind {
    literal,
    name,
    binary,
    unary,
    call,
    member,
    index,
    array,
    map
  } kind;
  Span span;
  Value literal;
  std::string name;
  Binary binary{};
  Unary unary{};
  Span operator_span;
  std::unique_ptr<Expr> left, right;
  std::vector<std::unique_ptr<Expr>> args;
  std::vector<std::unique_ptr<Expr>> elements;
  std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> entries;
};
struct Stmt {
  enum class Kind { var, assign, expr, if_, while_, ret, block } kind;
  Span span;
  std::string name;
  bool mutable_ = false;
  std::unique_ptr<Expr> expr, condition, target;
  std::vector<std::unique_ptr<Stmt>> body;
  std::unique_ptr<Stmt> then_branch, else_branch;
};
struct Function {
  Span span;
  std::string name;
  std::vector<std::string> params;
  std::unique_ptr<Stmt> body;
};
} // namespace thimble
