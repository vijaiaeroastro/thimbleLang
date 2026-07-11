Result<Value> index_value(const Value &object, const Value &index, Span s) {
  if (object.type() == Type::array) {
    auto i = index.as_int();
    if (!i)
      return i.error();
    const auto &values =
        std::get<std::shared_ptr<ArrayValue>>(object.data())->values;
    if (i.value() < 0 || static_cast<std::size_t>(i.value()) >= values.size())
      return err(ErrorCategory::runtime, "index_out_of_range",
                 "array index out of range", s);
    return values[static_cast<std::size_t>(i.value())];
  }
  if (object.type() == Type::map) {
    if (!index.is_acyclic())
      return err(ErrorCategory::runtime, "cyclic_key",
                 "map keys cannot contain collection cycles", s);
    const auto &entries =
        std::get<std::shared_ptr<MapValue>>(object.data())->entries;
    for (const auto &entry : entries)
      if (entry.first == index)
        return entry.second;
    return err(ErrorCategory::runtime, "missing_key", "map key was not found",
               s);
  }
  return err(ErrorCategory::type, "type_mismatch", "value is not indexable", s);
}
Result<Value> member_value(const Value &object, const std::string &name,
                           Span s) {
  if (object.type() != Type::host_object)
    return err(ErrorCategory::type, "type_mismatch",
               "member access expects a host object", s);
  auto ref = std::get<std::shared_ptr<HostObject>>(object.data());
  auto descriptor = std::static_pointer_cast<ObjectDescriptor>(ref->descriptor);
  auto property = descriptor->properties.find(name);
  if (property != descriptor->properties.end()) {
    try {
      if (!charge(s))
        return last;
      auto result = property->second.getter(ref->instance);
      if (!check_control(s))
        return last;
      if (!result)
        return host_error(result.error(), s, ref->type_name + "." + name);
      if (result.value().type() == Type::real &&
          !std::isfinite(std::get<double>(result.value().data())))
        return host_error(err(ErrorCategory::host, "non_finite_real",
                              "host property returned a non-finite real", s),
                          s, ref->type_name + "." + name);
      return result;
    } catch (...) {
      auto error = err(ErrorCategory::host, "host_failure",
                       "host property getter threw an exception", s);
      return host_error(std::move(error), s, ref->type_name + "." + name);
    }
  }
  if (descriptor->methods.count(name))
    return err(ErrorCategory::runtime, "method_value",
               "methods are only callable", s);
  return err(ErrorCategory::name, "unknown_member",
             "unknown object member: " + name, s);
}
Result<Value> member_call(const Value &object, const std::string &name,
                          const std::vector<Value> &args, Span s) {
  if (object.type() != Type::host_object)
    return err(ErrorCategory::type, "type_mismatch",
               "method call expects a host object", s);
  auto ref = std::get<std::shared_ptr<HostObject>>(object.data());
  auto descriptor = std::static_pointer_cast<ObjectDescriptor>(ref->descriptor);
  auto method = descriptor->methods.find(name);
  if (method == descriptor->methods.end())
    return err(ErrorCategory::name, "unknown_member",
               "unknown object method: " + name, s);
  if (args.size() != method->second.arity)
    return err(ErrorCategory::type, "arity_mismatch",
               "wrong argument count for method", s);
  try {
    if (!charge(s))
      return last;
    auto result = method->second.call(ref->instance, args);
    if (!check_control(s))
      return last;
    if (!result) {
      return host_error(result.error(), s, ref->type_name + "." + name);
    }
    if (result.value().type() == Type::real &&
        !std::isfinite(std::get<double>(result.value().data())))
      return host_error(err(ErrorCategory::host, "non_finite_real",
                            "host method returned a non-finite real", s),
                        s, ref->type_name + "." + name);
    return result;
  } catch (...) {
    auto error = err(ErrorCategory::host, "host_failure",
                     "host method threw an exception", s);
    return host_error(std::move(error), s, ref->type_name + "." + name);
  }
}
Result<Value> builtin_call(const std::string &n, const std::vector<Value> &a,
                           Span s) {
  if (n == "len") {
    if (a.size() != 1)
      return err(ErrorCategory::type, "arity_mismatch",
                 "len expects one argument", s);
    if (a[0].type() == Type::string)
      return Value(
          static_cast<std::int64_t>(std::get<std::string>(a[0].data()).size()));
    if (a[0].type() == Type::array)
      return Value(static_cast<std::int64_t>(
          std::get<std::shared_ptr<ArrayValue>>(a[0].data())->values.size()));
    if (a[0].type() == Type::map)
      return Value(static_cast<std::int64_t>(
          std::get<std::shared_ptr<MapValue>>(a[0].data())->entries.size()));
    return err(ErrorCategory::type, "type_mismatch",
               "len expects string, array or map", s);
  }
  if (n == "push") {
    if (a.size() != 2 || a[0].type() != Type::array)
      return err(ErrorCategory::type, "type_mismatch",
                 "push expects an array and a value", s);
    auto &values = std::get<std::shared_ptr<ArrayValue>>(a[0].data())->values;
    if (detail::would_create_cycle(a[0], a[1]))
      return err(ErrorCategory::runtime, "cyclic_collection",
                 "collection mutation would create a cycle", s);
    if (values.size() >= lim.collection_size)
      return err(ErrorCategory::limit, "collection_limit",
                 "array size limit exceeded", s);
    if (!reserve_memory(sizeof(Value), s))
      return last;
    values.push_back(a[1]);
    return Value();
  }
  if (n == "pop") {
    if (a.size() != 1 || a[0].type() != Type::array)
      return err(ErrorCategory::type, "type_mismatch", "pop expects an array",
                 s);
    auto &values = std::get<std::shared_ptr<ArrayValue>>(a[0].data())->values;
    if (values.empty())
      return err(ErrorCategory::runtime, "empty_collection",
                 "cannot pop an empty array", s);
    auto result = values.back();
    values.pop_back();
    return result;
  }
  if (n == "remove") {
    if (a.size() != 2)
      return err(ErrorCategory::type, "arity_mismatch",
                 "remove expects two arguments", s);
    if (a[0].type() == Type::array) {
      auto i = a[1].as_int();
      if (!i)
        return i.error();
      auto &values = std::get<std::shared_ptr<ArrayValue>>(a[0].data())->values;
      if (i.value() < 0 || static_cast<std::size_t>(i.value()) >= values.size())
        return err(ErrorCategory::runtime, "index_out_of_range",
                   "array index out of range", s);
      auto result = values[static_cast<std::size_t>(i.value())];
      values.erase(values.begin() + i.value());
      return result;
    }
    if (a[0].type() == Type::map) {
      if (!a[1].is_acyclic())
        return err(ErrorCategory::runtime, "cyclic_key",
                   "map keys cannot contain collection cycles", s);
      auto &entries = std::get<std::shared_ptr<MapValue>>(a[0].data())->entries;
      for (auto i = entries.begin(); i != entries.end(); ++i)
        if (i->first == a[1]) {
          auto result = i->second;
          entries.erase(i);
          return result;
        }
      return err(ErrorCategory::runtime, "missing_key", "map key was not found",
                 s);
    }
    return err(ErrorCategory::type, "type_mismatch",
               "remove expects an array or map", s);
  }
  return err(ErrorCategory::name, "unknown_function", "unknown function: " + n,
             s);
}
