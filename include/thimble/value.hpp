#pragma once
#include "error.hpp"
#include <cstdint>
#include <cmath>
#include <string>
#include <variant>

namespace thimble {
/// Runtime types available to a Thimble script.
enum class Type { null_value, boolean, integer, real, string };
/// Owning tagged value exchanged between scripts and C++ host callbacks.
class Value {
  std::variant<std::monostate,bool,std::int64_t,double,std::string> data_;
public:
  /// Construct the Thimble `null` value.
  Value():data_(std::monostate{}){} Value(std::nullptr_t):Value(){} Value(bool v):data_(v){}
  Value(std::int64_t v):data_(v){} Value(int v):data_(std::int64_t(v)){}
  Value(double v):data_(v){} Value(std::string v):data_(std::move(v)){} Value(const char* v):data_(std::string(v)){}
  /// Return the active runtime type.
  Type type() const { switch(data_.index()){case 0:return Type::null_value;case 1:return Type::boolean;case 2:return Type::integer;case 3:return Type::real;default:return Type::string;} }
  /// Convenience check for the null value.
  bool is_null()const{return type()==Type::null_value;}
  /// Expose the tagged storage for internal adapters and checked host code.
  const auto& data()const{return data_;}
  /// Checked accessors. A type mismatch is returned as a normal `Result` error.
  Result<bool> as_bool()const { if(auto p=std::get_if<bool>(&data_))return *p; return make_error(ErrorCategory::type,"type_mismatch","expected bool"); }
  Result<std::int64_t> as_int()const { if(auto p=std::get_if<std::int64_t>(&data_))return *p; return make_error(ErrorCategory::type,"type_mismatch","expected int"); }
  Result<double> as_real()const { if(auto p=std::get_if<double>(&data_))return *p; return make_error(ErrorCategory::type,"type_mismatch","expected real"); }
  Result<std::string> as_string()const { if(auto p=std::get_if<std::string>(&data_))return *p; return make_error(ErrorCategory::type,"type_mismatch","expected string"); }
  bool operator==(const Value& o)const{return data_==o.data_;}
};
/// Stable display spelling for a runtime type.
inline const char* type_name(Type t){switch(t){case Type::null_value:return "null";case Type::boolean:return "bool";case Type::integer:return "int";case Type::real:return "real";default:return "string";}}
}
