// Evaluation is deliberately left-to-right. The logical operators are the
// only operations which can skip evaluating their right-hand expression.
Result<Value> eval(const Expr &x, std::shared_ptr<Environment> env) {
  if (!charge(x.span))
    return last;
  switch (x.kind) {
  case Expr::Kind::literal:
    if (x.literal.type() == Type::string &&
        !reserve_memory(std::get<std::string>(x.literal.data()).size(), x.span))
      return last;
    return x.literal;
  case Expr::Kind::name: {
    auto b = env->find(x.name);
    if (!b || !b->initialized)
      return err(ErrorCategory::name, "unknown_name",
                 "unknown or uninitialized name: " + x.name, x.span);
    return b->value;
  }
  case Expr::Kind::call: {
    if (!x.left &&
        (x.name == "push" || x.name == "pop" || x.name == "remove") &&
        !x.args.empty() && x.args[0]->kind == Expr::Kind::name) {
      auto binding = env->find(x.args[0]->name);
      if (!binding || !binding->mutable_)
        return err(ErrorCategory::name, "immutable_assignment",
                   "collection binding is immutable", x.span);
    }
    std::vector<Value> a;
    a.reserve(x.args.size());
    for (auto &e : x.args) {
      auto r = eval(*e, env);
      if (!r)
        return r;
      a.push_back(r.value());
    }
    if (x.left && x.left->kind == Expr::Kind::member) {
      auto object = eval(*x.left->left, env);
      if (!object)
        return object;
      return member_call(object.value(), x.left->name, a, x.span);
    }
    return call(x.name, a, x.span);
  }
  case Expr::Kind::unary: {
    auto a = eval(*x.left, env);
    if (!a)
      return a;
    if (x.unary == Unary::not_) {
      if (a.value().type() != Type::boolean)
        return err(ErrorCategory::type, "type_mismatch", "! expects bool",
                   x.operator_span);
      return Value(!std::get<bool>(a.value().data()));
    }
    if (a.value().type() == Type::integer) {
      auto v = std::get<std::int64_t>(a.value().data());
      if (v == std::numeric_limits<std::int64_t>::min())
        return err(ErrorCategory::runtime, "integer_overflow",
                   "integer negation overflow", x.operator_span);
      return Value(-v);
    }
    if (a.value().type() == Type::real) {
      double v = -std::get<double>(a.value().data());
      if (!std::isfinite(v))
        return err(ErrorCategory::runtime, "non_finite_real", "non-finite real",
                   x.operator_span);
      return Value(v);
    }
    return err(ErrorCategory::type, "type_mismatch", "- expects int or real",
               x.operator_span);
  }
  case Expr::Kind::binary:
    return binary(x, *x.left, *x.right, env);
  case Expr::Kind::array: {
    if (!reserve_memory(x.elements.size() * sizeof(Value), x.span))
      return last;
    std::vector<Value> values;
    values.reserve(x.elements.size());
    for (const auto &e : x.elements) {
      if (values.size() >= lim.collection_size)
        return err(ErrorCategory::limit, "collection_limit",
                   "array size limit exceeded", x.span);
      auto v = eval(*e, env);
      if (!v)
        return v;
      values.push_back(v.value());
    }
    return Value::array(std::move(values));
  }
  case Expr::Kind::map: {
    if (!reserve_memory(x.entries.size() * sizeof(std::pair<Value, Value>),
                        x.span))
      return last;
    std::vector<std::pair<Value, Value>> entries;
    entries.reserve(x.entries.size());
    for (const auto &entry : x.entries) {
      if (entries.size() >= lim.collection_size)
        return err(ErrorCategory::limit, "collection_limit",
                   "map size limit exceeded", x.span);
      auto k = eval(*entry.first, env);
      if (!k)
        return k;
      if (!k.value().is_acyclic())
        return err(ErrorCategory::runtime, "cyclic_key",
                   "map keys cannot contain collection cycles", x.span);
      auto v = eval(*entry.second, env);
      if (!v)
        return v;
      entries.emplace_back(k.value(), v.value());
    }
    return Value::map(std::move(entries));
  }
  case Expr::Kind::index: {
    auto object = eval(*x.left, env);
    if (!object)
      return object;
    auto index = eval(*x.right, env);
    if (!index)
      return index;
    return index_value(object.value(), index.value(), x.span);
  }
  case Expr::Kind::member: {
    auto object = eval(*x.left, env);
    if (!object)
      return object;
    return member_value(object.value(), x.name, x.span);
  }
  }
  return err(ErrorCategory::runtime, "internal_error", "invalid expression",
             x.span);
}
Result<Value> binary(const Expr &x, const Expr &a, const Expr &b,
                     std::shared_ptr<Environment> env) {
  auto l = eval(a, env);
  if (!l)
    return l;
  if (x.binary == Binary::and_ || x.binary == Binary::or_) {
    if (l.value().type() != Type::boolean)
      return err(ErrorCategory::type, "type_mismatch",
                 "logical operator expects bool", x.operator_span);
    bool lv = std::get<bool>(l.value().data());
    if (x.binary == Binary::and_ && !lv)
      return Value(false);
    if (x.binary == Binary::or_ && lv)
      return Value(true);
    auto r = eval(b, env);
    if (!r)
      return r;
    if (r.value().type() != Type::boolean)
      return err(ErrorCategory::type, "type_mismatch",
                 "logical operator expects bool", x.operator_span);
    return r.value();
  }
  auto r = eval(b, env);
  if (!r)
    return r;
  auto lt = l.value().type(), rt = r.value().type();
  if (x.binary == Binary::eq || x.binary == Binary::ne) {
    if (lt != rt)
      return err(ErrorCategory::type, "type_mismatch",
                 "equality requires equal types", x.operator_span);
    bool v = l.value() == r.value();
    return Value(x.binary == Binary::eq ? v : !v);
  }
  if (lt == Type::integer && rt == Type::integer) {
    auto a1 = std::get<std::int64_t>(l.value().data()),
         b1 = std::get<std::int64_t>(r.value().data());
    auto mn = std::numeric_limits<std::int64_t>::min(),
         mx = std::numeric_limits<std::int64_t>::max();
    switch (x.binary) {
    case Binary::add:
      if ((b1 > 0 && a1 > mx - b1) || (b1 < 0 && a1 < mn - b1))
        return err(ErrorCategory::runtime, "integer_overflow",
                   "integer addition overflow", x.operator_span);
      return Value(a1 + b1);
    case Binary::sub:
      if ((b1 > 0 && a1 < mn + b1) || (b1 < 0 && a1 > mx + b1))
        return err(ErrorCategory::runtime, "integer_overflow",
                   "integer subtraction overflow", x.operator_span);
      return Value(a1 - b1);
    case Binary::mul:
      if (a1 == 0 || b1 == 0)
        return Value(std::int64_t(0));
      if ((a1 == mn && b1 == -1) || (b1 == mn && a1 == -1))
        return err(ErrorCategory::runtime, "integer_overflow",
                   "integer multiplication overflow", x.operator_span);
      if ((a1 > 0 && b1 > 0 && a1 > mx / b1) ||
          (a1 > 0 && b1 < 0 && b1 < mn / a1) ||
          (a1 < 0 && b1 > 0 && a1 < mn / b1) ||
          (a1 < 0 && b1 < 0 && b1 < mx / a1))
        return err(ErrorCategory::runtime, "integer_overflow",
                   "integer multiplication overflow", x.operator_span);
      return Value(a1 * b1);
    case Binary::div:
      if (!b1)
        return err(ErrorCategory::runtime, "division_by_zero",
                   "division by zero", x.operator_span);
      if (a1 == mn && b1 == -1)
        return err(ErrorCategory::runtime, "integer_overflow",
                   "integer division overflow", x.operator_span);
      return Value(a1 / b1);
    case Binary::mod:
      if (!b1)
        return err(ErrorCategory::runtime, "division_by_zero",
                   "remainder by zero", x.operator_span);
      return Value(a1 % b1);
    case Binary::lt:
      return Value(a1 < b1);
    case Binary::le:
      return Value(a1 <= b1);
    case Binary::gt:
      return Value(a1 > b1);
    case Binary::ge:
      return Value(a1 >= b1);
    default:
      break;
    }
  }
  if (lt == Type::real && rt == Type::real) {
    double a1 = std::get<double>(l.value().data()),
           b1 = std::get<double>(r.value().data()), z = 0;
    switch (x.binary) {
    case Binary::add:
      z = a1 + b1;
      break;
    case Binary::sub:
      z = a1 - b1;
      break;
    case Binary::mul:
      z = a1 * b1;
      break;
    case Binary::div:
      if (b1 == 0)
        return err(ErrorCategory::runtime, "division_by_zero",
                   "division by zero", x.operator_span);
      z = a1 / b1;
      break;
    case Binary::lt:
      return Value(a1 < b1);
    case Binary::le:
      return Value(a1 <= b1);
    case Binary::gt:
      return Value(a1 > b1);
    case Binary::ge:
      return Value(a1 >= b1);
    default:
      return err(ErrorCategory::type, "type_mismatch", "invalid real operator",
                 x.operator_span);
    }
    if (!std::isfinite(z))
      return err(ErrorCategory::runtime, "non_finite_real",
                 "non-finite real result", x.operator_span);
    return Value(z);
  }
  if (lt == Type::string && rt == Type::string && x.binary == Binary::add) {
    const auto &left = std::get<std::string>(l.value().data());
    const auto &right = std::get<std::string>(r.value().data());
    if (right.size() > lim.string_size ||
        left.size() > lim.string_size - right.size())
      return err(ErrorCategory::limit, "string_limit",
                 "string size limit exceeded", x.operator_span);
    if (!reserve_memory(left.size() + right.size(), x.operator_span))
      return last;
    return Value(left + right);
  }
  if (lt == Type::string && rt == Type::string &&
      (x.binary == Binary::lt || x.binary == Binary::le ||
       x.binary == Binary::gt || x.binary == Binary::ge)) {
    auto a1 = std::get<std::string>(l.value().data()),
         b1 = std::get<std::string>(r.value().data());
    if (x.binary == Binary::lt)
      return Value(a1 < b1);
    if (x.binary == Binary::le)
      return Value(a1 <= b1);
    if (x.binary == Binary::gt)
      return Value(a1 > b1);
    return Value(a1 >= b1);
  }
  return err(ErrorCategory::type, "type_mismatch",
             "operator operands have incompatible types", x.operator_span);
}
