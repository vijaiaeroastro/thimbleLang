#pragma once
#include "ast.hpp"
#include "lexer.hpp"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>

namespace thimble {
/// Recursive-descent parser for the grammar in SPEC.md.
class Parser {
  std::vector<Token> t_; std::size_t p_=0; Error error_{}; bool failed_=false;
  const Token& cur()const{return t_[p_];} bool is(Tok k)const{return cur().kind==k;}
  Token take(){return t_[p_++];}
  void fail(std::string c,std::string m){if(!failed_){failed_=true;error_=make_error(ErrorCategory::syntax,std::move(c),std::move(m),cur().span);}}
  bool eat(Tok k){if(is(k)){++p_;return true;}return false;}
  bool need(Tok k,const char* m){if(eat(k))return true;fail("unexpected_token",m);return false;}
  // One method is used for each precedence level. This makes associativity
  // explicit and produces an ownership-safe unique_ptr AST.
  std::unique_ptr<Expr> expr(){return logic_or();}
  std::unique_ptr<Expr> logic_or(){auto x=logic_and();while(!failed_&&eat(Tok::or_)){auto y=logic_and();x=bin(Binary::or_,std::move(x),std::move(y));}return x;}
  std::unique_ptr<Expr> logic_and(){auto x=equality();while(!failed_&&eat(Tok::and_)){auto y=equality();x=bin(Binary::and_,std::move(x),std::move(y));}return x;}
  std::unique_ptr<Expr> equality(){auto x=comparison();while(!failed_&&(is(Tok::eq)||is(Tok::ne))){auto k=take().kind;auto y=comparison();x=bin(k==Tok::eq?Binary::eq:Binary::ne,std::move(x),std::move(y));}return x;}
  std::unique_ptr<Expr> comparison(){auto x=term();while(!failed_&&(is(Tok::lt)||is(Tok::le)||is(Tok::gt)||is(Tok::ge))){auto k=take().kind;auto y=term();x=bin(k==Tok::lt?Binary::lt:k==Tok::le?Binary::le:k==Tok::gt?Binary::gt:Binary::ge,std::move(x),std::move(y));}return x;}
  std::unique_ptr<Expr> term(){auto x=factor();while(!failed_&&(is(Tok::plus)||is(Tok::minus))){auto k=take().kind;auto y=factor();x=bin(k==Tok::plus?Binary::add:Binary::sub,std::move(x),std::move(y));}return x;}
  std::unique_ptr<Expr> factor(){auto x=unary();while(!failed_&&(is(Tok::star)||is(Tok::slash)||is(Tok::percent))){auto k=take().kind;auto y=unary();x=bin(k==Tok::star?Binary::mul:k==Tok::slash?Binary::div:Binary::mod,std::move(x),std::move(y));}return x;}
  std::unique_ptr<Expr> unary(){if(is(Tok::minus)||is(Tok::not_)){auto tok=take();auto x=std::make_unique<Expr>();x->kind=Expr::Kind::unary;x->span=tok.span;x->unary=tok.kind==Tok::minus?Unary::neg:Unary::not_;x->left=unary();return x;}return primary();}
  std::unique_ptr<Expr> primary(){
    if(is(Tok::int_lit)||is(Tok::real_lit)||is(Tok::str_lit)||is(Tok::kw_true)||is(Tok::kw_false)||is(Tok::kw_null)){auto q=take();auto x=std::make_unique<Expr>();x->kind=Expr::Kind::literal;x->span=q.span; if(q.kind==Tok::str_lit)x->literal=Value(q.text);else if(q.kind==Tok::kw_true)x->literal=Value(true);else if(q.kind==Tok::kw_false)x->literal=Value(false);else if(q.kind==Tok::kw_null)x->literal=Value();else if(q.kind==Tok::int_lit){errno=0;long long v=std::strtoll(q.text.c_str(),nullptr,10);if(errno||v<0){fail("invalid_literal","integer literal out of range");return x;}x->literal=Value(std::int64_t(v));}else{errno=0;char* e=nullptr;double v=std::strtod(q.text.c_str(),&e);if(errno||!e||*e||!std::isfinite(v)){fail("invalid_literal","real literal out of range");return x;}x->literal=Value(v);}return x;}
    if(is(Tok::id)){auto q=take();if(eat(Tok::lparen)){auto x=std::make_unique<Expr>();x->kind=Expr::Kind::call;x->span=q.span;x->name=q.text;if(!is(Tok::rparen)){do{x->args.push_back(expr());}while(eat(Tok::comma));}need(Tok::rparen,"expected ')' after arguments");return x;}auto x=std::make_unique<Expr>();x->kind=Expr::Kind::name;x->span=q.span;x->name=q.text;return x;}
    if(eat(Tok::lparen)){auto x=expr();need(Tok::rparen,"expected ')' after expression");return x;}
    fail("unexpected_token","expected expression");return std::make_unique<Expr>();
  }
  std::unique_ptr<Expr> bin(Binary op,std::unique_ptr<Expr> a,std::unique_ptr<Expr> b){auto x=std::make_unique<Expr>();x->kind=Expr::Kind::binary;x->span=a?a->span:Span{};x->binary=op;x->left=std::move(a);x->right=std::move(b);return x;}
  std::unique_ptr<Stmt> statement(){
    if(is(Tok::kw_let)||is(Tok::kw_var)){auto q=take();if(!is(Tok::id)){fail("expected_name","expected variable name");return {}; }auto n=take();auto s=std::make_unique<Stmt>();s->kind=Stmt::Kind::var;s->span=q.span;s->name=n.text;s->mutable_=q.kind==Tok::kw_var;need(Tok::assign,"expected '='");s->expr=expr();need(Tok::semi,"expected ';'");return s;}
    if(is(Tok::id)&&p_+1<t_.size()&&t_[p_+1].kind==Tok::assign){auto n=take();take();auto s=std::make_unique<Stmt>();s->kind=Stmt::Kind::assign;s->span=n.span;s->name=n.text;s->expr=expr();need(Tok::semi,"expected ';'");return s;}
    if(is(Tok::kw_if)){auto q=take();auto s=std::make_unique<Stmt>();s->kind=Stmt::Kind::if_;s->span=q.span;need(Tok::lparen,"expected '('");s->condition=expr();need(Tok::rparen,"expected ')'");s->then_branch=block();if(eat(Tok::kw_else))s->else_branch=is(Tok::kw_if)?statement():block();return s;}
    if(is(Tok::kw_while)){auto q=take();auto s=std::make_unique<Stmt>();s->kind=Stmt::Kind::while_;s->span=q.span;need(Tok::lparen,"expected '('");s->condition=expr();need(Tok::rparen,"expected ')'");s->then_branch=block();return s;}
    if(is(Tok::kw_return)){auto q=take();auto s=std::make_unique<Stmt>();s->kind=Stmt::Kind::ret;s->span=q.span;if(!is(Tok::semi))s->expr=expr();need(Tok::semi,"expected ';'");return s;}
    if(is(Tok::lbrace))return block();
    auto s=std::make_unique<Stmt>();s->kind=Stmt::Kind::expr;s->span=cur().span;s->expr=expr();need(Tok::semi,"expected ';'");return s;
  }
  std::unique_ptr<Stmt> block(){auto b=std::make_unique<Stmt>();b->kind=Stmt::Kind::block;b->span=cur().span;need(Tok::lbrace,"expected '{'");while(!failed_&&!is(Tok::rbrace)&&!is(Tok::end))b->body.push_back(statement());need(Tok::rbrace,"expected '}'");return b;}
public:
  explicit Parser(std::vector<Token> t):t_(std::move(t)){}
  /// Parse top-level statements and function declarations.
  Result<std::pair<std::vector<std::unique_ptr<Stmt>>,std::vector<Function>>> parse(){std::vector<std::unique_ptr<Stmt>> stmts;std::vector<Function> funcs;while(!failed_&&!is(Tok::end)){if(eat(Tok::kw_fn)){auto f=Function{};f.span=t_[p_-1].span;if(!is(Tok::id)){fail("expected_name","expected function name");break;}f.name=take().text;need(Tok::lparen,"expected '('");if(!is(Tok::rparen)){do{if(!is(Tok::id)){fail("expected_name","expected parameter name");break;}f.params.push_back(take().text);}while(eat(Tok::comma));}need(Tok::rparen,"expected ')'");f.body=block();funcs.push_back(std::move(f));}else stmts.push_back(statement());}if(!failed_&&!is(Tok::end))fail("unexpected_token","unexpected token");if(failed_)return error_;return std::make_pair(std::move(stmts),std::move(funcs));}
};
}
