#pragma once

#include <cstdint>
#include <string_view>

namespace cpplox {

enum class TokenType : uint8_t {
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
  For,
  Fun,
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

  Error,
  Eof
};

inline constexpr TokenType TOKEN_LEFT_PAREN = TokenType::LeftParen;
inline constexpr TokenType TOKEN_RIGHT_PAREN = TokenType::RightParen;
inline constexpr TokenType TOKEN_LEFT_BRACE = TokenType::LeftBrace;
inline constexpr TokenType TOKEN_RIGHT_BRACE = TokenType::RightBrace;
inline constexpr TokenType TOKEN_COMMA = TokenType::Comma;
inline constexpr TokenType TOKEN_DOT = TokenType::Dot;
inline constexpr TokenType TOKEN_MINUS = TokenType::Minus;
inline constexpr TokenType TOKEN_PLUS = TokenType::Plus;
inline constexpr TokenType TOKEN_SEMICOLON = TokenType::Semicolon;
inline constexpr TokenType TOKEN_SLASH = TokenType::Slash;
inline constexpr TokenType TOKEN_STAR = TokenType::Star;
inline constexpr TokenType TOKEN_BANG = TokenType::Bang;
inline constexpr TokenType TOKEN_BANG_EQUAL = TokenType::BangEqual;
inline constexpr TokenType TOKEN_EQUAL = TokenType::Equal;
inline constexpr TokenType TOKEN_EQUAL_EQUAL = TokenType::EqualEqual;
inline constexpr TokenType TOKEN_GREATER = TokenType::Greater;
inline constexpr TokenType TOKEN_GREATER_EQUAL = TokenType::GreaterEqual;
inline constexpr TokenType TOKEN_LESS = TokenType::Less;
inline constexpr TokenType TOKEN_LESS_EQUAL = TokenType::LessEqual;
inline constexpr TokenType TOKEN_IDENTIFIER = TokenType::Identifier;
inline constexpr TokenType TOKEN_STRING = TokenType::String;
inline constexpr TokenType TOKEN_NUMBER = TokenType::Number;
inline constexpr TokenType TOKEN_AND = TokenType::And;
inline constexpr TokenType TOKEN_CLASS = TokenType::Class;
inline constexpr TokenType TOKEN_ELSE = TokenType::Else;
inline constexpr TokenType TOKEN_FALSE = TokenType::False;
inline constexpr TokenType TOKEN_FOR = TokenType::For;
inline constexpr TokenType TOKEN_FUN = TokenType::Fun;
inline constexpr TokenType TOKEN_IF = TokenType::If;
inline constexpr TokenType TOKEN_NIL = TokenType::Nil;
inline constexpr TokenType TOKEN_OR = TokenType::Or;
inline constexpr TokenType TOKEN_PRINT = TokenType::Print;
inline constexpr TokenType TOKEN_RETURN = TokenType::Return;
inline constexpr TokenType TOKEN_SUPER = TokenType::Super;
inline constexpr TokenType TOKEN_THIS = TokenType::This;
inline constexpr TokenType TOKEN_TRUE = TokenType::True;
inline constexpr TokenType TOKEN_VAR = TokenType::Var;
inline constexpr TokenType TOKEN_WHILE = TokenType::While;
inline constexpr TokenType TOKEN_ERROR = TokenType::Error;
inline constexpr TokenType TOKEN_EOF = TokenType::Eof;

inline constexpr int tokenIndex(TokenType type) {
  return static_cast<int>(type);
}

struct Token {
  TokenType type;
  const char *start;
  int length;
  int line;
};

class Scanner {
public:
  Scanner() = default;
  explicit Scanner(std::string_view source) { reset(source); }

  void reset(std::string_view source);
  Token scanToken();

private:
  static bool isAlpha(char c);
  static bool isDigit(char c);

  bool isAtEnd() const;
  char advance();
  char peek() const;
  char peekNext() const;
  bool match(char expected);
  Token makeToken(TokenType type) const;
  Token errorToken(const char *message) const;
  void skipWhitespace();
  TokenType checkKeyword(int start, int length, const char *rest,
                         TokenType type) const;
  TokenType identifierType() const;
  Token identifier();
  Token number();
  Token string();

  const char *start_ = "";
  const char *current_ = "";
  const char *end_ = "";
  int line_ = 1;
};

} // namespace cpplox
