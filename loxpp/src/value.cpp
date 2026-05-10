#include "value.h"

#include "callable.h"
#include "lox_instance.h"
#include "token.h"

namespace loxpp {

std::string astLiteralToString(const Value& value) {
  if (std::holds_alternative<std::monostate>(value)) {
    return "nil";
  }
  if (const auto* boolean = std::get_if<bool>(&value)) {
    return *boolean ? "true" : "false";
  }
  if (const auto* number = std::get_if<double>(&value)) {
    return formatJavaNumber(*number, true);
  }
  if (const auto* text = std::get_if<std::string>(&value)) {
    return *text;
  }
  return "<object>";
}

std::string stringifyValue(const Value& value) {
  if (std::holds_alternative<std::monostate>(value)) {
    return "nil";
  }
  if (const auto* boolean = std::get_if<bool>(&value)) {
    return *boolean ? "true" : "false";
  }
  if (const auto* number = std::get_if<double>(&value)) {
    std::string text = formatJavaNumber(*number, true);
    if (text.size() >= 2 && text.ends_with(".0")) {
      text.resize(text.size() - 2);
    }
    return text;
  }
  if (const auto* text = std::get_if<std::string>(&value)) {
    return *text;
  }
  if (const auto* callable = std::get_if<std::shared_ptr<LoxCallable>>(&value)) {
    return (*callable)->toString();
  }
  return std::get<std::shared_ptr<LoxInstance>>(value)->toString();
}

bool isTruthy(const Value& value) {
  if (std::holds_alternative<std::monostate>(value)) {
    return false;
  }
  if (const auto* boolean = std::get_if<bool>(&value)) {
    return *boolean;
  }
  return true;
}

bool isEqual(const Value& left, const Value& right) {
  if (std::holds_alternative<std::monostate>(left) &&
      std::holds_alternative<std::monostate>(right)) {
    return true;
  }
  if (left.index() != right.index()) {
    return false;
  }
  if (const auto* left_bool = std::get_if<bool>(&left)) {
    return *left_bool == std::get<bool>(right);
  }
  if (const auto* left_number = std::get_if<double>(&left)) {
    return *left_number == std::get<double>(right);
  }
  if (const auto* left_string = std::get_if<std::string>(&left)) {
    return *left_string == std::get<std::string>(right);
  }
  if (const auto* left_callable =
          std::get_if<std::shared_ptr<LoxCallable>>(&left)) {
    return *left_callable == std::get<std::shared_ptr<LoxCallable>>(right);
  }
  if (const auto* left_instance =
          std::get_if<std::shared_ptr<LoxInstance>>(&left)) {
    return *left_instance == std::get<std::shared_ptr<LoxInstance>>(right);
  }
  return false;
}

}  // namespace loxpp
