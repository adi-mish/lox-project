#include "token.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace loxpp {

std::string_view tokenTypeName(TokenType type) {
  switch (type) {
    case TokenType::LeftParen:
      return "LEFT_PAREN";
    case TokenType::RightParen:
      return "RIGHT_PAREN";
    case TokenType::LeftBrace:
      return "LEFT_BRACE";
    case TokenType::RightBrace:
      return "RIGHT_BRACE";
    case TokenType::Comma:
      return "COMMA";
    case TokenType::Dot:
      return "DOT";
    case TokenType::Minus:
      return "MINUS";
    case TokenType::Plus:
      return "PLUS";
    case TokenType::Semicolon:
      return "SEMICOLON";
    case TokenType::Slash:
      return "SLASH";
    case TokenType::Star:
      return "STAR";
    case TokenType::Bang:
      return "BANG";
    case TokenType::BangEqual:
      return "BANG_EQUAL";
    case TokenType::Equal:
      return "EQUAL";
    case TokenType::EqualEqual:
      return "EQUAL_EQUAL";
    case TokenType::Greater:
      return "GREATER";
    case TokenType::GreaterEqual:
      return "GREATER_EQUAL";
    case TokenType::Less:
      return "LESS";
    case TokenType::LessEqual:
      return "LESS_EQUAL";
    case TokenType::Identifier:
      return "IDENTIFIER";
    case TokenType::String:
      return "STRING";
    case TokenType::Number:
      return "NUMBER";
    case TokenType::And:
      return "AND";
    case TokenType::Class:
      return "CLASS";
    case TokenType::Else:
      return "ELSE";
    case TokenType::False:
      return "FALSE";
    case TokenType::Fun:
      return "FUN";
    case TokenType::For:
      return "FOR";
    case TokenType::If:
      return "IF";
    case TokenType::Nil:
      return "NIL";
    case TokenType::Or:
      return "OR";
    case TokenType::Print:
      return "PRINT";
    case TokenType::Return:
      return "RETURN";
    case TokenType::Super:
      return "SUPER";
    case TokenType::This:
      return "THIS";
    case TokenType::True:
      return "TRUE";
    case TokenType::Var:
      return "VAR";
    case TokenType::While:
      return "WHILE";
    case TokenType::Eof:
      return "EOF";
  }
  return "EOF";
}

std::string formatJavaNumber(double value, bool keep_integral_suffix) {
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value < 0 ? "-Infinity" : "Infinity";
  }

  std::ostringstream stream;
  stream << std::setprecision(15) << value;
  std::string text = stream.str();

  if (text.find_first_of(".eE") == std::string::npos &&
      keep_integral_suffix) {
    text += ".0";
  }

  return text;
}

std::string literalToString(const TokenLiteral& literal) {
  if (std::holds_alternative<std::monostate>(literal)) {
    return "null";
  }
  if (const auto* number = std::get_if<double>(&literal)) {
    return formatJavaNumber(*number, true);
  }
  return std::get<std::string>(literal);
}

}  // namespace loxpp
