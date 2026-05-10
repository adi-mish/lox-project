#include "value.h"

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

}  // namespace loxpp
