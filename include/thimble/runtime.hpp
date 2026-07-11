#pragma once
#include "ast.hpp"
#include "host.hpp"
#include "parser.hpp"
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace thimble {
/// A binding stored in one lexical environment.
struct Binding { Value value; bool mutable_=false; bool initialized=true; };
/// Lexical scope chain. Child scopes own only their parent pointer; values are
/// copied when a block is entered, while function calls retain the root chain.
struct Env : std::enable_shared_from_this<Env> {
  std::shared_ptr<Env> parent; std::unordered_map<std::string,Binding> vars;
  explicit Env(std::shared_ptr<Env> p={}):parent(std::move(p)){}
  Binding* find(const std::string& n){auto i=vars.find(n);if(i!=vars.end())return &i->second;return parent?parent->find(n):nullptr;}
};
struct Flow { bool returned=false, failed=false; Value value; Error error; static Flow ok(){return {};} };

class Program {
  std::string source_; std::vector<std::unique_ptr<Stmt>> top_; std::vector<Function> funcs_;
  std::unordered_map<std::string,std::size_t> fidx_;
  friend Result<Program> compile(std::string,const HostContext&);
  struct Runner {
    const Program& p; const HostContext& host; Limits lim; std::size_t steps=0; std::shared_ptr<Env> root;
    Runner(const Program& pp,const HostContext& hh,Limits ll):p(pp),host(hh),lim(ll){}
    Error err(ErrorCategory c,std::string code,std::string msg,Span s){auto e=make_error(c,std::move(code),std::move(msg),s,p.source_);return e;}
    // Keep the counter in the runner, so every execution has independent limits
    // even when the same immutable Program is run concurrently.
    bool charge(Span s){if(steps>=lim.steps){last=err(ErrorCategory::limit,"step_limit","execution step limit exceeded",s);return false;}++steps;return true;}
    Error last{};
    // Evaluation is deliberately left-to-right. The logical operators are the
    // only operations which can skip evaluating their right-hand expression.
    Result<Value> eval(const Expr& x,std::shared_ptr<Env> env){
      if(!charge(x.span))return last;
      switch(x.kind){
      case Expr::Kind::literal:return x.literal;
      case Expr::Kind::name:{auto b=env->find(x.name);if(!b||!b->initialized)return err(ErrorCategory::name,"unknown_name","unknown or uninitialized name: "+x.name,x.span);return b->value;}
      case Expr::Kind::call:{std::vector<Value> a;for(auto& e:x.args){auto r=eval(*e,env);if(!r)return r;a.push_back(r.value());}return call(x.name,a,x.span);}
      case Expr::Kind::unary:{auto a=eval(*x.left,env);if(!a)return a;if(x.unary==Unary::not_){if(a.value().type()!=Type::boolean)return err(ErrorCategory::type,"type_mismatch","! expects bool",x.span);return Value(!std::get<bool>(a.value().data()));}if(a.value().type()==Type::integer){auto v=std::get<std::int64_t>(a.value().data());if(v==std::numeric_limits<std::int64_t>::min())return err(ErrorCategory::runtime,"integer_overflow","integer negation overflow",x.span);return Value(-v);}if(a.value().type()==Type::real){double v=-std::get<double>(a.value().data());if(!std::isfinite(v))return err(ErrorCategory::runtime,"non_finite_real","non-finite real",x.span);return Value(v);}return err(ErrorCategory::type,"type_mismatch","- expects int or real",x.span);}
      case Expr::Kind::binary:return binary(x,*x.left,*x.right,env);
      } return err(ErrorCategory::runtime,"internal_error","invalid expression",x.span);
    }
    Result<Value> binary(const Expr& x,const Expr& a,const Expr& b,std::shared_ptr<Env> env){
      auto l=eval(a,env);if(!l)return l;
      if(x.binary==Binary::and_||x.binary==Binary::or_){if(l.value().type()!=Type::boolean)return err(ErrorCategory::type,"type_mismatch","logical operator expects bool",x.span);bool lv=std::get<bool>(l.value().data());if(x.binary==Binary::and_&&!lv)return Value(false);if(x.binary==Binary::or_&&lv)return Value(true);auto r=eval(b,env);if(!r)return r;if(r.value().type()!=Type::boolean)return err(ErrorCategory::type,"type_mismatch","logical operator expects bool",x.span);return r.value();}
      auto r=eval(b,env);if(!r)return r;auto lt=l.value().type(),rt=r.value().type();
      if(x.binary==Binary::eq||x.binary==Binary::ne){if(lt!=rt)return err(ErrorCategory::type,"type_mismatch","equality requires equal types",x.span);bool v=l.value()==r.value();return Value(x.binary==Binary::eq?v:!v);}
      if(lt==Type::integer&&rt==Type::integer){auto a1=std::get<std::int64_t>(l.value().data()),b1=std::get<std::int64_t>(r.value().data());auto mn=std::numeric_limits<std::int64_t>::min(),mx=std::numeric_limits<std::int64_t>::max();switch(x.binary){case Binary::add:if((b1>0&&a1>mx-b1)||(b1<0&&a1<mn-b1))return err(ErrorCategory::runtime,"integer_overflow","integer addition overflow",x.span);return Value(a1+b1);case Binary::sub:if((b1>0&&a1<mn+b1)||(b1<0&&a1>mx+b1))return err(ErrorCategory::runtime,"integer_overflow","integer subtraction overflow",x.span);return Value(a1-b1);case Binary::mul:if(a1==0||b1==0)return Value(std::int64_t(0));if((a1==mn&&b1==-1)||(b1==mn&&a1==-1))return err(ErrorCategory::runtime,"integer_overflow","integer multiplication overflow",x.span);if((a1>0&&b1>0&&a1>mx/b1)||(a1>0&&b1<0&&b1<mn/a1)||(a1<0&&b1>0&&a1<mn/b1)||(a1<0&&b1<0&&b1<mx/a1))return err(ErrorCategory::runtime,"integer_overflow","integer multiplication overflow",x.span);return Value(a1*b1);case Binary::div:if(!b1)return err(ErrorCategory::runtime,"division_by_zero","division by zero",x.span);if(a1==mn&&b1==-1)return err(ErrorCategory::runtime,"integer_overflow","integer division overflow",x.span);return Value(a1/b1);case Binary::mod:if(!b1)return err(ErrorCategory::runtime,"division_by_zero","remainder by zero",x.span);return Value(a1%b1);case Binary::lt:return Value(a1<b1);case Binary::le:return Value(a1<=b1);case Binary::gt:return Value(a1>b1);case Binary::ge:return Value(a1>=b1);default:break;}}
      if(lt==Type::real&&rt==Type::real){double a1=std::get<double>(l.value().data()),b1=std::get<double>(r.value().data()),z=0;switch(x.binary){case Binary::add:z=a1+b1;break;case Binary::sub:z=a1-b1;break;case Binary::mul:z=a1*b1;break;case Binary::div:if(b1==0)return err(ErrorCategory::runtime,"division_by_zero","division by zero",x.span);z=a1/b1;break;case Binary::lt:return Value(a1<b1);case Binary::le:return Value(a1<=b1);case Binary::gt:return Value(a1>b1);case Binary::ge:return Value(a1>=b1);default:return err(ErrorCategory::type,"type_mismatch","invalid real operator",x.span);}if(!std::isfinite(z))return err(ErrorCategory::runtime,"non_finite_real","non-finite real result",x.span);return Value(z);}
      if(lt==Type::string&&rt==Type::string&&x.binary==Binary::add)return Value(std::get<std::string>(l.value().data())+std::get<std::string>(r.value().data()));
      if(lt==Type::string&&rt==Type::string&&(x.binary==Binary::lt||x.binary==Binary::le||x.binary==Binary::gt||x.binary==Binary::ge)){auto a1=std::get<std::string>(l.value().data()),b1=std::get<std::string>(r.value().data());if(x.binary==Binary::lt)return Value(a1<b1);if(x.binary==Binary::le)return Value(a1<=b1);if(x.binary==Binary::gt)return Value(a1>b1);return Value(a1>=b1);}
      return err(ErrorCategory::type,"type_mismatch","operator operands have incompatible types",x.span);
    }
    Result<Value> call(const std::string& n,const std::vector<Value>& a,Span s){
      auto hi=host.functions().find(n);auto fi=p.fidx_.find(n);if(fi!=p.fidx_.end()){auto& f=p.funcs_[fi->second];if(a.size()!=f.params.size())return err(ErrorCategory::type,"arity_mismatch","wrong argument count",s);if(depth>=lim.call_depth)return err(ErrorCategory::limit,"call_depth_limit","call-depth limit exceeded",s);++depth;auto child=std::make_shared<Env>(root);for(std::size_t i=0;i<a.size();++i)child->vars.emplace(f.params[i],Binding{a[i],false,true});auto flow=exec(*f.body,child);--depth;if(flow.failed)return flow.error;return flow.returned?flow.value:Value();}
      if(hi!=host.functions().end()){if(a.size()!=hi->second.first)return err(ErrorCategory::type,"arity_mismatch","wrong argument count",s);try{auto r=hi->second.second(a);if(!r)return r.error();if(r.value().type()==Type::real&&!std::isfinite(std::get<double>(r.value().data())))return err(ErrorCategory::host,"non_finite_real","host returned a non-finite real",s);return r.value();}catch(...){return err(ErrorCategory::host,"host_failure","host callback threw an exception",s);}}
      return err(ErrorCategory::name,"unknown_name","unknown function: "+n,s);
    }
    std::size_t depth=0;
    Flow exec(const Stmt& s,std::shared_ptr<Env> env){if(!charge(s.span))return Flow{false,true,{},last};switch(s.kind){case Stmt::Kind::var:{auto r=eval(*s.expr,env);if(!r)return Flow{false,true,{},r.error()};env->vars[s.name]=Binding{r.value(),s.mutable_,true};return Flow::ok();}case Stmt::Kind::assign:{auto r=eval(*s.expr,env);if(!r)return Flow{false,true,{},r.error()};auto b=env->find(s.name);if(!b)return Flow{false,true,{},err(ErrorCategory::name,"unknown_name","unknown name: "+s.name,s.span)};if(!b->mutable_)return Flow{false,true,{},err(ErrorCategory::name,"immutable_assignment","cannot assign immutable name",s.span)};if(!charge(s.span))return Flow{false,true,{},last};b->value=r.value();return Flow::ok();}case Stmt::Kind::expr:{auto r=eval(*s.expr,env);if(!r)return Flow{false,true,{},r.error()};return Flow::ok();}case Stmt::Kind::ret:{if(!s.expr)return Flow{true,false,Value(),{}};auto r=eval(*s.expr,env);if(!r)return Flow{false,true,{},r.error()};return Flow{true,false,r.value(),{}};}case Stmt::Kind::block:{auto child=std::make_shared<Env>(env);for(auto& x:s.body){auto f=exec(*x,child);if(f.failed||f.returned)return f;}return Flow::ok();}case Stmt::Kind::if_:{auto c=eval(*s.condition,env);if(!c)return Flow{false,true,{},c.error()};if(c.value().type()!=Type::boolean)return Flow{false,true,{},err(ErrorCategory::type,"type_mismatch","if condition expects bool",s.span)};if(std::get<bool>(c.value().data()))return exec(*s.then_branch,env);if(s.else_branch)return exec(*s.else_branch,env);return Flow::ok();}case Stmt::Kind::while_:{for(;;){auto c=eval(*s.condition,env);if(!c)return Flow{false,true,{},c.error()};if(c.value().type()!=Type::boolean)return Flow{false,true,{},err(ErrorCategory::type,"type_mismatch","while condition expects bool",s.span)};if(!std::get<bool>(c.value().data()))return Flow::ok();auto f=exec(*s.then_branch,env);if(f.failed||f.returned)return f;}}}return Flow::ok();}
    Result<Value> run(){root=std::make_shared<Env>();for(auto& kv:host.values()){if(kv.second.type()==Type::real&&!std::isfinite(std::get<double>(kv.second.data())))return err(ErrorCategory::host,"non_finite_real","host supplied a non-finite real",Span{});root->vars.emplace(kv.first,Binding{kv.second,false,true});}for(auto& s:p.top_){auto f=exec(*s,root);if(f.failed)return f.error;if(f.returned)return f.value;}return Value();}
  };
public:
  Program()=default; Program(Program&&)=default; Program& operator=(Program&&)=default; Program(const Program&)=delete;
  /// Execute with fresh script state and the supplied host context.
  Result<Value> execute(const HostContext& host,Limits limits={})const {Runner r(*this,host,limits);return r.run();}
};
/// Compile source against the names and callable arities in a host context.
inline Result<Program> compile(std::string source,const HostContext& host={}){
  auto lx=Lexer(source).run();
  if(!lx) return lx.error();
  auto parsed=Parser(std::move(lx.value())).parse();
  if(!parsed) return parsed.error();
  Program out; out.source_=std::move(source); out.top_=std::move(parsed.value().first); out.funcs_=std::move(parsed.value().second);
  for(std::size_t i=0;i<out.funcs_.size();++i){
    if(out.fidx_.count(out.funcs_[i].name)) return make_error(ErrorCategory::name,"duplicate_name","duplicate function: "+out.funcs_[i].name,out.funcs_[i].span,out.source_);
    if(host.functions().count(out.funcs_[i].name)) return make_error(ErrorCategory::name,"duplicate_name","function conflicts with host function: "+out.funcs_[i].name,out.funcs_[i].span,out.source_);
    std::unordered_set<std::string> seen;
    for(const auto& name:out.funcs_[i].params) if(!seen.insert(name).second) return make_error(ErrorCategory::name,"duplicate_name","duplicate parameter: "+name,out.funcs_[i].span,out.source_);
    out.fidx_[out.funcs_[i].name]=i;
  }
  using Vars=std::unordered_map<std::string,bool>;
  Vars root_vars; for(const auto& kv:host.values()) root_vars.emplace(kv.first,false);
  std::function<Error(const Expr&,const Vars&)> check_expr;
  std::function<Error(const Stmt&,Vars)> check_stmt;
  auto has_error=[](const Error& e){return !e.code.empty();};
  check_expr=[&](const Expr& e,const Vars& vars)->Error{
    if(e.kind==Expr::Kind::name){if(vars.find(e.name)==vars.end()&&host.values().find(e.name)==host.values().end())return make_error(ErrorCategory::name,"unknown_name","unknown name: "+e.name,e.span,out.source_);return {};}
    if(e.kind==Expr::Kind::call){auto fi=out.fidx_.find(e.name);auto hi=host.functions().find(e.name);if(fi==out.fidx_.end()&&hi==host.functions().end())return make_error(ErrorCategory::name,"unknown_name","unknown function: "+e.name,e.span,out.source_);auto arity=fi!=out.fidx_.end()?out.funcs_[fi->second].params.size():hi->second.first;if(e.args.size()!=arity)return make_error(ErrorCategory::type,"arity_mismatch","wrong argument count for "+e.name,e.span,out.source_);for(const auto& a:e.args){auto z=check_expr(*a,vars);if(has_error(z))return z;}return {};}
    if(e.kind==Expr::Kind::unary)return check_expr(*e.left,vars);
    if(e.kind==Expr::Kind::binary){auto z=check_expr(*e.left,vars);if(has_error(z))return z;return check_expr(*e.right,vars);}
    return {};
  };
  check_stmt=[&](const Stmt& s,Vars vars)->Error{
    if(s.kind==Stmt::Kind::var){auto z=check_expr(*s.expr,vars);if(has_error(z))return z;vars[s.name]=s.mutable_;return {};}
    if(s.kind==Stmt::Kind::assign){auto it=vars.find(s.name);if(it==vars.end()&&host.values().find(s.name)==host.values().end())return make_error(ErrorCategory::name,"unknown_name","unknown name: "+s.name,s.span,out.source_);if(it!=vars.end()&&!it->second)return make_error(ErrorCategory::name,"immutable_assignment","cannot assign immutable name",s.span,out.source_);return check_expr(*s.expr,vars);}
    if(s.kind==Stmt::Kind::expr||s.kind==Stmt::Kind::ret)return s.expr?check_expr(*s.expr,vars):Error{};
    if(s.kind==Stmt::Kind::block){for(const auto& x:s.body){auto z=check_stmt(*x,vars);if(has_error(z))return z;}return {};}
    if(s.kind==Stmt::Kind::if_){auto z=check_expr(*s.condition,vars);if(has_error(z))return z;z=check_stmt(*s.then_branch,vars);if(has_error(z))return z;return s.else_branch?check_stmt(*s.else_branch,vars):Error{};}
    if(s.kind==Stmt::Kind::while_){auto z=check_expr(*s.condition,vars);if(has_error(z))return z;return check_stmt(*s.then_branch,vars);}
    return {};
  };
  for(const auto& s:out.top_) if(s->kind==Stmt::Kind::var){if(root_vars.count(s->name)&&host.values().find(s->name)==host.values().end())return make_error(ErrorCategory::name,"duplicate_name","duplicate variable: "+s->name,s->span,out.source_);root_vars[s->name]=s->mutable_;}
  for(const auto& f:out.funcs_){Vars vars=root_vars;for(const auto& n:f.params)vars[n]=false;auto z=check_stmt(*f.body,vars);if(has_error(z))return z;}
  Vars vars=root_vars;for(const auto& s:out.top_){auto z=check_stmt(*s,vars);if(has_error(z))return z;if(s->kind==Stmt::Kind::var)vars[s->name]=s->mutable_;}
  return out;
}
}
