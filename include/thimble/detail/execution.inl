Result<Value> call(const std::string &n, const std::vector<Value> &a, Span s) {
  if (!charge(s))
    return last;
  if (n == "len" || n == "push" || n == "pop" || n == "remove")
    return builtin_call(n, a, s);
  auto hi = host.functions().find(n);
  auto fi = p.fidx_.find(n);
  if (fi != p.fidx_.end()) {
    auto &f = p.funcs_[fi->second];
    if (a.size() != f.params.size())
      return err(ErrorCategory::type, "arity_mismatch", "wrong argument count",
                 s);
    if (depth >= lim.call_depth)
      return err(ErrorCategory::limit, "call_depth_limit",
                 "call-depth limit exceeded", s);
    ++depth;
    auto child = std::make_shared<Environment>(root);
    for (std::size_t i = 0; i < a.size(); ++i)
      child->variables.emplace(f.params[i], Binding{a[i], false, true});
    auto flow = exec(*f.body, child);
    --depth;
    if (flow.failed) {
      flow.error.trace.push_back({f.name, p.source_name_, s});
      return flow.error;
    }
    return flow.returned ? flow.value : Value();
  }
  if (hi != host.functions().end()) {
    if (a.size() != hi->second.first)
      return err(ErrorCategory::type, "arity_mismatch", "wrong argument count",
                 s);
    try {
      if (!check_control(s))
        return last;
      auto r = hi->second.second(a);
      if (!check_control(s))
        return last;
      if (!r) {
        return host_error(r.error(), s, n);
      }
      if (r.value().type() == Type::real &&
          !std::isfinite(std::get<double>(r.value().data())))
        return host_error(err(ErrorCategory::host, "non_finite_real",
                              "host returned a non-finite real", s),
                          s, n);
      return r.value();
    } catch (...) {
      auto error = err(ErrorCategory::host, "host_failure",
                       "host callback threw an exception", s);
      return host_error(std::move(error), s, n);
    }
  }
  return err(ErrorCategory::name, "unknown_name", "unknown function: " + n, s);
}
std::size_t depth = 0;
Result<Value> assign_target(const Expr &target, const Value &value,
                            std::shared_ptr<Environment> env, Span s) {
  if (target.kind == Expr::Kind::name) {
    auto b = env->find(target.name);
    if (!b)
      return err(ErrorCategory::name, "unknown_name",
                 "unknown name: " + target.name, s);
    if (!b->mutable_)
      return err(ErrorCategory::name, "immutable_assignment",
                 "cannot assign immutable name", s);
    b->value = value;
    return Value();
  }
  if (target.kind == Expr::Kind::member) {
    auto object = eval(*target.left, env);
    if (!object)
      return object;
    auto ref = object.value().as_object();
    if (!ref)
      return ref.error();
    auto descriptor =
        std::static_pointer_cast<ObjectDescriptor>(ref.value()->descriptor);
    auto property = descriptor->properties.find(target.name);
    if (property == descriptor->properties.end() || !property->second.setter)
      return err(ErrorCategory::name, "immutable_property",
                 "property is read-only", s);
    try {
      if (!charge(s))
        return last;
      auto result = property->second.setter(ref.value()->instance, value);
      if (!check_control(s))
        return last;
      if (!result)
        return host_error(result.error(), s,
                          ref.value()->type_name + "." + target.name);
      return result;
    } catch (...) {
      auto error = err(ErrorCategory::host, "host_failure",
                       "host property setter threw an exception", s);
      return host_error(std::move(error), s,
                        ref.value()->type_name + "." + target.name);
    }
  }
  if (target.kind == Expr::Kind::index) {
    auto object = eval(*target.left, env);
    if (!object)
      return object;
    auto index = eval(*target.right, env);
    if (!index)
      return index;
    if (target.left->kind == Expr::Kind::name) {
      auto b = env->find(target.left->name);
      if (!b || !b->mutable_)
        return err(ErrorCategory::name, "immutable_assignment",
                   "collection binding is immutable", s);
    }
    if (object.value().type() == Type::array) {
      auto i = index.value().as_int();
      if (!i)
        return i.error();
      auto &values =
          std::get<std::shared_ptr<ArrayValue>>(object.value().data())->values;
      if (i.value() < 0 || static_cast<std::size_t>(i.value()) >= values.size())
        return err(ErrorCategory::runtime, "index_out_of_range",
                   "array index out of range", s);
      if (detail::would_create_cycle(object.value(), value))
        return err(ErrorCategory::runtime, "cyclic_collection",
                   "collection mutation would create a cycle", s);
      values[static_cast<std::size_t>(i.value())] = value;
      return Value();
    }
    if (object.value().type() == Type::map) {
      if (!index.value().is_acyclic())
        return err(ErrorCategory::runtime, "cyclic_key",
                   "map keys cannot contain collection cycles", s);
      if (detail::would_create_cycle(object.value(), index.value()) ||
          detail::would_create_cycle(object.value(), value))
        return err(ErrorCategory::runtime, "cyclic_collection",
                   "collection mutation would create a cycle", s);
      auto &entries =
          std::get<std::shared_ptr<MapValue>>(object.value().data())->entries;
      for (auto &entry : entries)
        if (entry.first == index.value()) {
          entry.second = value;
          return Value();
        }
      if (entries.size() >= lim.collection_size)
        return err(ErrorCategory::limit, "collection_limit",
                   "map size limit exceeded", s);
      if (!reserve_memory(sizeof(std::pair<Value, Value>), s))
        return last;
      entries.emplace_back(index.value(), value);
      return Value();
    }
    return err(ErrorCategory::type, "type_mismatch", "value is not indexable",
               s);
  }
  return err(ErrorCategory::syntax, "invalid_assignment",
             "invalid assignment target", s);
}
Flow exec(const Stmt &s, std::shared_ptr<Environment> env) {
  if (!charge(s.span))
    return Flow{false, true, {}, last};
  switch (s.kind) {
  case Stmt::Kind::var: {
    auto r = eval(*s.expr, env);
    if (!r)
      return Flow{false, true, {}, r.error()};
    env->variables[s.name] = Binding{r.value(), s.mutable_, true};
    return Flow::success();
  }
  case Stmt::Kind::assign: {
    auto r = eval(*s.expr, env);
    if (!r)
      return Flow{false, true, {}, r.error()};
    auto assigned = assign_target(*s.target, r.value(), env, s.span);
    if (!assigned)
      return Flow{false, true, {}, assigned.error()};
    if (!charge(s.span))
      return Flow{false, true, {}, last};
    return Flow::success();
  }
  case Stmt::Kind::expr: {
    auto r = eval(*s.expr, env);
    if (!r)
      return Flow{false, true, {}, r.error()};
    return Flow::success();
  }
  case Stmt::Kind::ret: {
    if (!s.expr)
      return Flow{true, false, Value(), {}};
    auto r = eval(*s.expr, env);
    if (!r)
      return Flow{false, true, {}, r.error()};
    return Flow{true, false, r.value(), {}};
  }
  case Stmt::Kind::block: {
    auto child = std::make_shared<Environment>(env);
    for (auto &x : s.body) {
      auto f = exec(*x, child);
      if (f.failed || f.returned)
        return f;
    }
    return Flow::success();
  }
  case Stmt::Kind::if_: {
    auto c = eval(*s.condition, env);
    if (!c)
      return Flow{false, true, {}, c.error()};
    if (c.value().type() != Type::boolean)
      return Flow{false,
                  true,
                  {},
                  err(ErrorCategory::type, "type_mismatch",
                      "if condition expects bool", s.condition->span)};
    if (std::get<bool>(c.value().data()))
      return exec(*s.then_branch, env);
    if (s.else_branch)
      return exec(*s.else_branch, env);
    return Flow::success();
  }
  case Stmt::Kind::while_: {
    for (;;) {
      auto c = eval(*s.condition, env);
      if (!c)
        return Flow{false, true, {}, c.error()};
      if (c.value().type() != Type::boolean)
        return Flow{false,
                    true,
                    {},
                    err(ErrorCategory::type, "type_mismatch",
                        "while condition expects bool", s.condition->span)};
      if (!std::get<bool>(c.value().data()))
        return Flow::success();
      auto f = exec(*s.then_branch, env);
      if (f.failed || f.returned)
        return f;
    }
  }
  }
  return Flow::success();
}
Result<Value> run() {
  root = std::make_shared<Environment>();
  for (auto &kv : host.values()) {
    if (kv.second.type() == Type::real &&
        !std::isfinite(std::get<double>(kv.second.data())))
      return err(ErrorCategory::host, "non_finite_real",
                 "host supplied a non-finite real", Span{});
    root->variables.emplace(kv.first, Binding{kv.second, false, true});
  }
  for (auto &s : p.top_) {
    auto f = exec(*s, root);
    if (f.failed)
      return f.error;
    if (f.returned)
      return f.value;
  }
  return Value();
}
