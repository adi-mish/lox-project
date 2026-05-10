#ifndef clox_scanner_h
#define clox_scanner_h

namespace cpplox {

typedef enum {

  TOKEN_LEFT_PAREN,
  TOKEN_RIGHT_PAREN,
  TOKEN_LEFT_BRACE,
  TOKEN_RIGHT_BRACE,
  TOKEN_COMMA,
  TOKEN_DOT,
  TOKEN_MINUS,
  TOKEN_PLUS,
  TOKEN_SEMICOLON,
  TOKEN_SLASH,
  TOKEN_STAR,

  TOKEN_BANG,
  TOKEN_BANG_EQUAL,
  TOKEN_EQUAL,
  TOKEN_EQUAL_EQUAL,
  TOKEN_GREATER,
  TOKEN_GREATER_EQUAL,
  TOKEN_LESS,
  TOKEN_LESS_EQUAL,

  TOKEN_IDENTIFIER,
  TOKEN_STRING,
  TOKEN_NUMBER,

  TOKEN_AND,
  TOKEN_CLASS,
  TOKEN_ELSE,
  TOKEN_FALSE,
  TOKEN_FOR,
  TOKEN_FUN,
  TOKEN_IF,
  TOKEN_NIL,
  TOKEN_OR,
  TOKEN_PRINT,
  TOKEN_RETURN,
  TOKEN_SUPER,
  TOKEN_THIS,
  TOKEN_TRUE,
  TOKEN_VAR,
  TOKEN_WHILE,

  TOKEN_ERROR,
  TOKEN_EOF
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  int length;
  int line;
} Token;

class Scanner {
public:
  Scanner() = default;
  explicit Scanner(const char *source) { reset(source); }

  void reset(const char *source);
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
  int line_ = 1;
};

} // namespace cpplox

#endif
