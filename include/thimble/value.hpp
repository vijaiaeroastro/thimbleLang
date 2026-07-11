#pragma once
#include "error.hpp"
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
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
enum class Type { null_value, boolean, integer, real, string, array, map, host_object };

/// Owning tagged value exchanged between scripts and C++ host callbacks.
class Value {
  std::variant<std::monostate,bool,std::int64_t,double,std::string,
               std::shared_ptr<ArrayValue>,std::shared_ptr<MapValue>,
               std::shared_ptr<HostObject>> data_;
public:
  Value():data_(std::monostate{}){}
  Value(std::nullptr_t):Value(){}
  Value(bool v):data_(v){}
  Value(std::int64_t v):data_(v){}
  Value(int v):data_(std::int64_t(v)){}
  Value(double v):data_(v){}
  Value(std::string v):data_(std::move(v)){}
  Value(const char* v):data_(std::string(v)){}

  /// Construct an owned, mutable array value.
  static Value array(std::vector<Value> values);
  /// Construct an owned, mutable map value.
  static Value map(std::vector<std::pair<Value,Value>> entries);
  /// Construct an opaque host object value.
  static Value object(std::shared_ptr<void> instance,
                      std::shared_ptr<void> descriptor,
                      std::string type_name);

  /// Return the active runtime type.
  Type type() const;
  bool is_null() const { return type()==Type::null_value; }
  const auto& data() const { return data_; }
  Result<bool> as_bool() const;
  Result<std::int64_t> as_int() const;
  Result<double> as_real() const;
  Result<std::string> as_string() const;
  Result<std::shared_ptr<ArrayValue>> as_array() const;
  Result<std::shared_ptr<MapValue>> as_map() const;
  Result<std::shared_ptr<HostObject>> as_object() const;
  bool operator==(const Value& other) const;
};

struct ArrayValue { std::vector<Value> values; };
struct MapValue { std::vector<std::pair<Value,Value>> entries; };

inline Value Value::array(std::vector<Value> values) {
  Value result;
  result.data_ = std::shared_ptr<ArrayValue>(new ArrayValue{std::move(values)});
  return result;
}
inline Value Value::map(std::vector<std::pair<Value,Value>> entries) {
  Value result;
  result.data_ = std::shared_ptr<MapValue>(new MapValue{std::move(entries)});
  return result;
}
inline Value Value::object(std::shared_ptr<void> instance,
                           std::shared_ptr<void> descriptor,
                           std::string type_name) {
  Value result;
  result.data_ = std::shared_ptr<HostObject>(new HostObject{
      std::move(instance), std::move(descriptor), std::move(type_name)});
  return result;
}
inline Type Value::type() const {
  switch (data_.index()) {
    case 0: return Type::null_value;
    case 1: return Type::boolean;
    case 2: return Type::integer;
    case 3: return Type::real;
    case 4: return Type::string;
    case 5: return Type::array;
    case 6: return Type::map;
    default: return Type::host_object;
  }
}
inline Result<bool> Value::as_bool() const {
  if (auto p=std::get_if<bool>(&data_)) return *p;
  return make_error(ErrorCategory::type,"type_mismatch","expected bool");
}
inline Result<std::int64_t> Value::as_int() const {
  if (auto p=std::get_if<std::int64_t>(&data_)) return *p;
  return make_error(ErrorCategory::type,"type_mismatch","expected int");
}
inline Result<double> Value::as_real() const {
  if (auto p=std::get_if<double>(&data_)) return *p;
  return make_error(ErrorCategory::type,"type_mismatch","expected real");
}
inline Result<std::string> Value::as_string() const {
  if (auto p=std::get_if<std::string>(&data_)) return *p;
  return make_error(ErrorCategory::type,"type_mismatch","expected string");
}
inline Result<std::shared_ptr<ArrayValue>> Value::as_array() const {
  if (auto p=std::get_if<std::shared_ptr<ArrayValue>>(&data_)) return *p;
  return make_error(ErrorCategory::type,"type_mismatch","expected array");
}
inline Result<std::shared_ptr<MapValue>> Value::as_map() const {
  if (auto p=std::get_if<std::shared_ptr<MapValue>>(&data_)) return *p;
  return make_error(ErrorCategory::type,"type_mismatch","expected map");
}
inline Result<std::shared_ptr<HostObject>> Value::as_object() const {
  if (auto p=std::get_if<std::shared_ptr<HostObject>>(&data_)) return *p;
  return make_error(ErrorCategory::type,"type_mismatch","expected host object");
}

inline bool Value::operator==(const Value& other) const {
  if (type()!=other.type()) return false;
  if (type()==Type::array) {
    const auto& a=std::get<std::shared_ptr<ArrayValue>>(data_)->values;
    const auto& b=std::get<std::shared_ptr<ArrayValue>>(other.data_)->values;
    if (a.size()!=b.size()) return false;
    for (std::size_t i=0;i<a.size();++i) if (!(a[i]==b[i])) return false;
    return true;
  }
  if (type()==Type::map) {
    const auto& a=std::get<std::shared_ptr<MapValue>>(data_)->entries;
    const auto& b=std::get<std::shared_ptr<MapValue>>(other.data_)->entries;
    if (a.size()!=b.size()) return false;
    for (const auto& entry:a) {
      bool found=false;
      for (const auto& candidate:b) if (entry.first==candidate.first && entry.second==candidate.second) {found=true;break;}
      if (!found) return false;
    }
    return true;
  }
  if (type()==Type::host_object) {
    return std::get<std::shared_ptr<HostObject>>(data_)->instance ==
           std::get<std::shared_ptr<HostObject>>(other.data_)->instance;
  }
  return data_==other.data_;
}

inline const char* type_name(Type t) {
  switch(t) {
    case Type::null_value:return "null"; case Type::boolean:return "bool";
    case Type::integer:return "int"; case Type::real:return "real";
    case Type::string:return "string"; case Type::array:return "array";
    case Type::map:return "map"; default:return "host_object";
  }
}
}
