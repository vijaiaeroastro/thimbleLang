#pragma once
#include "error.hpp"
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace thimble {
struct ArrayValue;
struct MapValue;

/// An opaque C++ object held by a script. The descriptor is interpreted by the
/// host runtime; scripts can only use members explicitly registered there.
struct HostObject {
  std::shared_ptr<void> instance;
  std::shared_ptr<void> descriptor;
  std::string type_name;
};

/// Runtime types available to a Thimble script.
enum class Type {
  null_value,
  boolean,
  integer,
  real,
  string,
  array,
  map,
  host_object
};

/// Owning tagged value exchanged between scripts and C++ host callbacks.
class Value {
  std::variant<std::monostate, bool, std::int64_t, double, std::string,
               std::shared_ptr<ArrayValue>, std::shared_ptr<MapValue>,
               std::shared_ptr<HostObject>>
      data_;

public:
  Value() : data_(std::monostate{}) {}
  Value(std::nullptr_t) : Value() {}
  Value(bool v) : data_(v) {}
  Value(std::int64_t v) : data_(v) {}
  Value(int v) : data_(std::int64_t(v)) {}
  Value(double v) : data_(v) {}
  Value(std::string v) : data_(std::move(v)) {}
  Value(const char *v) : data_(std::string(v)) {}

  /// Construct an owned, mutable array value.
  static Value array(std::vector<Value> values);
  /// Construct an owned, mutable map value.
  static Value map(std::vector<std::pair<Value, Value>> entries);
  /// Construct an opaque host object value.
  static Value object(std::shared_ptr<void> instance,
                      std::shared_ptr<void> descriptor, std::string type_name);

  /// Return the active runtime type.
  Type type() const;
  bool is_null() const { return type() == Type::null_value; }
  const auto &data() const { return data_; }
  Result<bool> as_bool() const;
  Result<std::int64_t> as_int() const;
  Result<double> as_real() const;
  Result<std::string> as_string() const;
  Result<std::shared_ptr<ArrayValue>> as_array() const;
  Result<std::shared_ptr<MapValue>> as_map() const;
  Result<std::shared_ptr<HostObject>> as_object() const;
  /// True when this value does not contain an array or map cycle.
  bool is_acyclic() const;
  bool operator==(const Value &other) const;
};

struct ArrayValue {
  std::vector<Value> values;
};
struct MapValue {
  std::vector<std::pair<Value, Value>> entries;
};

inline Value Value::array(std::vector<Value> values) {
  Value result;
  result.data_ = std::make_shared<ArrayValue>(ArrayValue{std::move(values)});
  return result;
}
inline Value Value::map(std::vector<std::pair<Value, Value>> entries) {
  Value result;
  result.data_ = std::make_shared<MapValue>(MapValue{std::move(entries)});
  return result;
}
inline Value Value::object(std::shared_ptr<void> instance,
                           std::shared_ptr<void> descriptor,
                           std::string type_name) {
  Value result;
  result.data_ = std::make_shared<HostObject>(HostObject{
      std::move(instance), std::move(descriptor), std::move(type_name)});
  return result;
}
inline Type Value::type() const {
  switch (data_.index()) {
  case 0:
    return Type::null_value;
  case 1:
    return Type::boolean;
  case 2:
    return Type::integer;
  case 3:
    return Type::real;
  case 4:
    return Type::string;
  case 5:
    return Type::array;
  case 6:
    return Type::map;
  default:
    return Type::host_object;
  }
}
inline Result<bool> Value::as_bool() const {
  if (auto p = std::get_if<bool>(&data_))
    return *p;
  return make_error(ErrorCategory::type, "type_mismatch", "expected bool");
}
inline Result<std::int64_t> Value::as_int() const {
  if (auto p = std::get_if<std::int64_t>(&data_))
    return *p;
  return make_error(ErrorCategory::type, "type_mismatch", "expected int");
}
inline Result<double> Value::as_real() const {
  if (auto p = std::get_if<double>(&data_))
    return *p;
  return make_error(ErrorCategory::type, "type_mismatch", "expected real");
}
inline Result<std::string> Value::as_string() const {
  if (auto p = std::get_if<std::string>(&data_))
    return *p;
  return make_error(ErrorCategory::type, "type_mismatch", "expected string");
}
inline Result<std::shared_ptr<ArrayValue>> Value::as_array() const {
  if (auto p = std::get_if<std::shared_ptr<ArrayValue>>(&data_))
    return *p;
  return make_error(ErrorCategory::type, "type_mismatch", "expected array");
}
inline Result<std::shared_ptr<MapValue>> Value::as_map() const {
  if (auto p = std::get_if<std::shared_ptr<MapValue>>(&data_))
    return *p;
  return make_error(ErrorCategory::type, "type_mismatch", "expected map");
}
inline Result<std::shared_ptr<HostObject>> Value::as_object() const {
  if (auto p = std::get_if<std::shared_ptr<HostObject>>(&data_))
    return *p;
  return make_error(ErrorCategory::type, "type_mismatch",
                    "expected host object");
}

namespace detail {
struct ValueNode {
  Type type;
  const void *pointer;
  bool operator==(const ValueNode &other) const {
    return type == other.type && pointer == other.pointer;
  }
};
struct ValueNodeHash {
  std::size_t operator()(const ValueNode &node) const {
    return std::hash<const void *>{}(node.pointer) ^
           (static_cast<std::size_t>(node.type) << 1U);
  }
};
struct ValuePair {
  ValueNode left, right;
  bool operator==(const ValuePair &other) const {
    return left == other.left && right == other.right;
  }
};
struct ValuePairHash {
  std::size_t operator()(const ValuePair &pair) const {
    return ValueNodeHash{}(pair.left) ^ (ValueNodeHash{}(pair.right) << 1U);
  }
};

inline ValueNode node_of(const Value &value) {
  if (value.type() == Type::array)
    return {Type::array,
            std::get<std::shared_ptr<ArrayValue>>(value.data()).get()};
  return {Type::map, std::get<std::shared_ptr<MapValue>>(value.data()).get()};
}

inline bool acyclic(const Value &value,
                    std::unordered_set<ValueNode, ValueNodeHash> &active) {
  if (value.type() != Type::array && value.type() != Type::map)
    return true;
  const auto node = node_of(value);
  if (!active.insert(node).second)
    return false;
  bool result = true;
  if (value.type() == Type::array) {
    for (const auto &item :
         std::get<std::shared_ptr<ArrayValue>>(value.data())->values)
      if (!acyclic(item, active)) {
        result = false;
        break;
      }
  } else {
    for (const auto &entry :
         std::get<std::shared_ptr<MapValue>>(value.data())->entries)
      if (!acyclic(entry.first, active) || !acyclic(entry.second, active)) {
        result = false;
        break;
      }
  }
  active.erase(node);
  return result;
}

inline bool
references_node(const Value &value, const ValueNode &target,
                std::unordered_set<ValueNode, ValueNodeHash> &visited) {
  if (value.type() != Type::array && value.type() != Type::map)
    return false;
  const auto node = node_of(value);
  if (node == target)
    return true;
  if (!visited.insert(node).second)
    return false;
  if (value.type() == Type::array) {
    for (const auto &item :
         std::get<std::shared_ptr<ArrayValue>>(value.data())->values)
      if (references_node(item, target, visited))
        return true;
  } else {
    for (const auto &entry :
         std::get<std::shared_ptr<MapValue>>(value.data())->entries)
      if (references_node(entry.first, target, visited) ||
          references_node(entry.second, target, visited))
        return true;
  }
  return false;
}

inline bool would_create_cycle(const Value &container, const Value &child) {
  if ((container.type() != Type::array && container.type() != Type::map) ||
      (child.type() != Type::array && child.type() != Type::map))
    return false;
  std::unordered_set<ValueNode, ValueNodeHash> visited;
  return references_node(child, node_of(container), visited);
}

inline bool
value_equal(const Value &left, const Value &right,
            std::unordered_set<ValuePair, ValuePairHash> visited = {}) {
  if (left.type() != right.type())
    return false;
  if (left.type() == Type::array || left.type() == Type::map) {
    const ValuePair pair{node_of(left), node_of(right)};
    if (!visited.insert(pair).second)
      return true;
    if (left.type() == Type::array) {
      const auto &a =
          std::get<std::shared_ptr<ArrayValue>>(left.data())->values;
      const auto &b =
          std::get<std::shared_ptr<ArrayValue>>(right.data())->values;
      if (a.size() != b.size())
        return false;
      for (std::size_t i = 0; i < a.size(); ++i)
        if (!value_equal(a[i], b[i], visited))
          return false;
      return true;
    }
    const auto &a = std::get<std::shared_ptr<MapValue>>(left.data())->entries;
    const auto &b = std::get<std::shared_ptr<MapValue>>(right.data())->entries;
    if (a.size() != b.size())
      return false;
    std::vector<bool> matched(b.size(), false);
    for (const auto &entry : a) {
      bool found = false;
      for (std::size_t i = 0; i < b.size(); ++i) {
        if (!matched[i] && value_equal(entry.first, b[i].first, visited) &&
            value_equal(entry.second, b[i].second, visited)) {
          matched[i] = true;
          found = true;
          break;
        }
      }
      if (!found)
        return false;
    }
    return true;
  }
  if (left.type() == Type::host_object)
    return std::get<std::shared_ptr<HostObject>>(left.data())->instance ==
           std::get<std::shared_ptr<HostObject>>(right.data())->instance;
  return left.data() == right.data();
}
} // namespace detail

inline bool Value::is_acyclic() const {
  std::unordered_set<detail::ValueNode, detail::ValueNodeHash> active;
  return detail::acyclic(*this, active);
}

inline bool Value::operator==(const Value &other) const {
  return detail::value_equal(*this, other);
}

inline const char *type_name(Type t) {
  switch (t) {
  case Type::null_value:
    return "null";
  case Type::boolean:
    return "bool";
  case Type::integer:
    return "int";
  case Type::real:
    return "real";
  case Type::string:
    return "string";
  case Type::array:
    return "array";
  case Type::map:
    return "map";
  default:
    return "host_object";
  }
}
} // namespace thimble
