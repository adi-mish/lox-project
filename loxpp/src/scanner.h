#pragma once

#include <string>
#include <vector>

#include "error_reporter.h"
#include "token.h"

namespace loxpp {

class Scanner {
 public:
  Scanner(std::string source, ErrorReporter& reporter);

  std::vector<Token> scanTokens();

 private:
  [[nodiscard]] bool isAtEnd() const;
  char advance();
  void addToken(TokenType type);
  void addToken(TokenType type, TokenLiteral literal);
  bool match(char expected);
  [[nodiscard]] char peek() const;
  [[nodiscard]] char peekNext() const;
  void scanToken();
  void identifier();
  void string();
  void number();

  static bool isDigit(char c);
  static bool isAlpha(char c);
  static bool isAlphaNumeric(char c);

  std::string source_;
  ErrorReporter& reporter_;
  std::vector<Token> tokens_;
  std::size_t start_ = 0;
  std::size_t current_ = 0;
  int line_ = 1;
};

}  // namespace loxpp
