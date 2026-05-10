#pragma once

#include <string>
#include <string_view>
#include <variant>

namespace loxpp {

enum class TokenType {
  LeftParen,
  RightParen,
  LeftBrace,
  RightBrace,
  Comma,
  Dot,
  Minus,
  Plus,
  Semicolon,
  Slash,
  Star,
  Bang,
  BangEqual,
  Equal,
  EqualEqual,
  Greater,
  GreaterEqual,
  Less,
  LessEqual,
  Identifier,
  String,
  Number,
  And,
  Class,
  Else,
  False,
  Fun,
  For,
  If,
  Nil,
  Or,
  Print,
  Return,
  Super,
  This,
  True,
  Var,
  While,
  Eof,
};

using TokenLiteral = std::variant<std::monostate, double, std::string>;

struct Token {
  TokenType type;
  std::string lexeme;
  TokenLiteral literal;
  int line;
};

std::string_view tokenTypeName(TokenType type);
std::string formatJavaNumber(double value, bool keep_integral_suffix);
std::string literalToString(const TokenLiteral& literal);

}  // namespace loxpp
