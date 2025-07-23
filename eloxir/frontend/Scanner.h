#pragma once
#include "Token.h"
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace eloxir {

class Scanner {
public:
  explicit Scanner(std::string source);
  std::vector<Token> scanTokens();

private:
  using Literal = std::variant<std::monostate, double, std::string, bool>;

  std::string source;
  std::vector<Token> tokens;
  size_t start = 0;
  size_t current = 0;
  int line = 1;

  static const std::unordered_map<std::string, TokenType> keywords;

  bool isAtEnd() const { return current >= source.size(); }
  char advance();
  bool match(char expected);
  char peek() const;
  char peekNext() const;
  void addToken(TokenType type);
  void addToken(TokenType type, const Literal &literal);
  void scanToken();

  void string();
  void number();
  void identifier();
  void commentLine();

  static bool isAlpha(char c);
  static bool isAlphaNumeric(char c);
  static bool isDigit(char c);
};

} // namespace eloxir