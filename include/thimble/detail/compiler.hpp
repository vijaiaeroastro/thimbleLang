#pragma once

/// Compile named source against the names and callable arities in a host
/// context. The name is used only in diagnostics.
inline Result<Program> compile(std::string source, std::string source_name,
                               const HostContext &host) {
  auto lx = Lexer(source).run();
  if (!lx) {
    auto error = lx.error();
    error.source = source;
    error.source_name = source_name;
    return error;
  }
  auto parsed = Parser(std::move(lx.value())).parse();
  if (!parsed) {
    auto error = parsed.error();
    error.source = source;
    error.source_name = source_name;
    return error;
  }
  Program out;
  out.source_ = std::move(source);
  out.source_name_ = std::move(source_name);
  out.top_ = std::move(parsed.value().first);
  out.funcs_ = std::move(parsed.value().second);
  for (std::size_t i = 0; i < out.funcs_.size(); ++i) {
    if (out.fidx_.count(out.funcs_[i].name))
      return make_error(ErrorCategory::name, "duplicate_name",
                        "duplicate function: " + out.funcs_[i].name,
                        out.funcs_[i].span, out.source_, out.source_name_);
    if (host.functions().count(out.funcs_[i].name))
      return make_error(ErrorCategory::name, "duplicate_name",
                        "function conflicts with host function: " +
                            out.funcs_[i].name,
                        out.funcs_[i].span, out.source_, out.source_name_);
    std::unordered_set<std::string> seen;
    for (const auto &name : out.funcs_[i].params)
      if (!seen.insert(name).second)
        return make_error(ErrorCategory::name, "duplicate_name",
                          "duplicate parameter: " + name, out.funcs_[i].span,
                          out.source_, out.source_name_);
    out.fidx_[out.funcs_[i].name] = i;
  }
  using Vars = std::unordered_map<std::string, bool>;
  Vars root_vars;
  for (const auto &kv : host.values())
    root_vars.emplace(kv.first, false);
  std::function<Error(const Expr &, const Vars &)> check_expr;
  std::function<Error(const Stmt &, Vars &)> check_stmt;
  auto has_error = [](const Error &e) { return !e.code.empty(); };
  auto remember_object = [&](const std::string &name,
                             const Value &value) -> ObjectDescriptor * {
    if (value.type() != Type::host_object)
      return nullptr;
    auto object = value.as_object().value();
    auto descriptor =
        std::static_pointer_cast<ObjectDescriptor>(object->descriptor);
    Program::ObjectShape shape;
    shape.type_name = object->type_name;
    for (const auto &property : descriptor->properties)
      shape.properties.emplace(property.first,
                               static_cast<bool>(property.second.setter));
    for (const auto &method : descriptor->methods)
      shape.methods.emplace(method.first, method.second.arity);
    out.required_objects_[name] = std::move(shape);
    return descriptor.get();
  };
  check_expr = [&](const Expr &e, const Vars &vars) -> Error {
    if (e.kind == Expr::Kind::name) {
      if (vars.find(e.name) == vars.end() &&
          host.values().find(e.name) == host.values().end())
        return make_error(ErrorCategory::name, "unknown_name",
                          "unknown name: " + e.name, e.span, out.source_,
                          out.source_name_);
      auto host_value = host.values().find(e.name);
      if (host_value != host.values().end()) {
        out.required_host_values_.insert(e.name);
        remember_object(e.name, host_value->second);
      }
      return {};
    }
    if (e.kind == Expr::Kind::call) {
      if (e.left) {
        if (e.left->kind == Expr::Kind::member &&
            e.left->left->kind == Expr::Kind::name) {
          auto value = host.values().find(e.left->left->name);
          if (value != host.values().end()) {
            auto descriptor = remember_object(value->first, value->second);
            out.required_host_values_.insert(value->first);
            if (!descriptor || !descriptor->methods.count(e.left->name))
              return make_error(ErrorCategory::name, "unknown_member",
                                "unknown object method: " + e.left->name,
                                e.span, out.source_, out.source_name_);
            if (descriptor->methods.at(e.left->name).arity != e.args.size())
              return make_error(ErrorCategory::type, "arity_mismatch",
                                "wrong argument count for method", e.span,
                                out.source_, out.source_name_);
          }
        }
        auto z = check_expr(*e.left, vars);
        if (has_error(z))
          return z;
      } else {
        auto fi = out.fidx_.find(e.name);
        auto hi = host.functions().find(e.name);
        bool builtin = e.name == "len" || e.name == "push" || e.name == "pop" ||
                       e.name == "remove";
        if (fi == out.fidx_.end() && hi == host.functions().end() && !builtin)
          return make_error(ErrorCategory::name, "unknown_name",
                            "unknown function: " + e.name, e.span, out.source_,
                            out.source_name_);
        if (!builtin) {
          auto arity = fi != out.fidx_.end()
                           ? out.funcs_[fi->second].params.size()
                           : hi->second.first;
          if (e.args.size() != arity)
            return make_error(ErrorCategory::type, "arity_mismatch",
                              "wrong argument count for " + e.name, e.span,
                              out.source_, out.source_name_);
          if (hi != host.functions().end())
            out.required_host_functions_[e.name] = hi->second.first;
        }
      }
      for (const auto &a : e.args) {
        auto z = check_expr(*a, vars);
        if (has_error(z))
          return z;
      }
      return {};
    }
    if (e.kind == Expr::Kind::unary)
      return check_expr(*e.left, vars);
    if (e.kind == Expr::Kind::binary) {
      auto z = check_expr(*e.left, vars);
      if (has_error(z))
        return z;
      return check_expr(*e.right, vars);
    }
    if (e.kind == Expr::Kind::member) {
      auto z = check_expr(*e.left, vars);
      if (has_error(z))
        return z;
      if (e.left->kind == Expr::Kind::name) {
        auto value = host.values().find(e.left->name);
        if (value != host.values().end()) {
          auto descriptor = remember_object(value->first, value->second);
          if (!descriptor || (!descriptor->properties.count(e.name) &&
                              !descriptor->methods.count(e.name)))
            return make_error(ErrorCategory::name, "unknown_member",
                              "unknown object member: " + e.name, e.span,
                              out.source_, out.source_name_);
        }
      }
      return {};
    }
    if (e.kind == Expr::Kind::index) {
      auto z = check_expr(*e.left, vars);
      if (has_error(z))
        return z;
      return check_expr(*e.right, vars);
    }
    if (e.kind == Expr::Kind::array) {
      for (const auto &item : e.elements) {
        auto z = check_expr(*item, vars);
        if (has_error(z))
          return z;
      }
      return {};
    }
    if (e.kind == Expr::Kind::map) {
      for (const auto &entry : e.entries) {
        auto z = check_expr(*entry.first, vars);
        if (has_error(z))
          return z;
        z = check_expr(*entry.second, vars);
        if (has_error(z))
          return z;
      }
      return {};
    }
    return {};
  };
  check_stmt = [&](const Stmt &s, Vars &vars) -> Error {
    if (s.kind == Stmt::Kind::var) {
      auto z = check_expr(*s.expr, vars);
      if (has_error(z))
        return z;
      vars[s.name] = s.mutable_;
      return {};
    }
    if (s.kind == Stmt::Kind::assign) {
      if (s.target->kind == Expr::Kind::name) {
        auto it = vars.find(s.target->name);
        if (it == vars.end() &&
            host.values().find(s.target->name) == host.values().end())
          return make_error(ErrorCategory::name, "unknown_name",
                            "unknown name: " + s.target->name, s.span,
                            out.source_, out.source_name_);
        if (it != vars.end() && !it->second)
          return make_error(ErrorCategory::name, "immutable_assignment",
                            "cannot assign immutable name", s.span, out.source_,
                            out.source_name_);
      }
      if (s.target->kind == Expr::Kind::member &&
          s.target->left->kind == Expr::Kind::name) {
        auto value = host.values().find(s.target->left->name);
        if (value != host.values().end()) {
          auto descriptor = remember_object(value->first, value->second);
          out.required_host_values_.insert(value->first);
          if (!descriptor ||
              descriptor->properties.find(s.target->name) ==
                  descriptor->properties.end() ||
              !descriptor->properties.at(s.target->name).setter)
            return make_error(ErrorCategory::name, "immutable_property",
                              "property is not writable: " + s.target->name,
                              s.span, out.source_, out.source_name_);
        }
      }
      auto z = check_expr(*s.target, vars);
      if (has_error(z))
        return z;
      return check_expr(*s.expr, vars);
    }
    if (s.kind == Stmt::Kind::expr || s.kind == Stmt::Kind::ret)
      return s.expr ? check_expr(*s.expr, vars) : Error{};
    if (s.kind == Stmt::Kind::block) {
      Vars block_vars = vars;
      for (const auto &x : s.body) {
        auto z = check_stmt(*x, block_vars);
        if (has_error(z))
          return z;
      }
      return {};
    }
    if (s.kind == Stmt::Kind::if_) {
      auto z = check_expr(*s.condition, vars);
      if (has_error(z))
        return z;
      Vars then_vars = vars;
      z = check_stmt(*s.then_branch, then_vars);
      if (has_error(z))
        return z;
      if (!s.else_branch)
        return {};
      Vars else_vars = vars;
      return check_stmt(*s.else_branch, else_vars);
    }
    if (s.kind == Stmt::Kind::while_) {
      auto z = check_expr(*s.condition, vars);
      if (has_error(z))
        return z;
      Vars body_vars = vars;
      return check_stmt(*s.then_branch, body_vars);
    }
    return {};
  };
  for (const auto &s : out.top_)
    if (s->kind == Stmt::Kind::var) {
      if (root_vars.count(s->name) &&
          host.values().find(s->name) == host.values().end())
        return make_error(ErrorCategory::name, "duplicate_name",
                          "duplicate variable: " + s->name, s->span,
                          out.source_, out.source_name_);
      root_vars[s->name] = s->mutable_;
    }
  for (const auto &f : out.funcs_) {
    Vars vars = root_vars;
    for (const auto &n : f.params)
      vars[n] = false;
    auto z = check_stmt(*f.body, vars);
    if (has_error(z))
      return z;
  }
  Vars vars = root_vars;
  for (const auto &s : out.top_) {
    auto z = check_stmt(*s, vars);
    if (has_error(z))
      return z;
    if (s->kind == Stmt::Kind::var)
      vars[s->name] = s->mutable_;
  }
  return out;
}

/// Compile source without a display name.
inline Result<Program> compile(std::string source,
                               const HostContext &host = {}) {
  return compile(std::move(source), {}, host);
}
