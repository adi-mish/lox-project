#include "Scanner.h"
#include <cctype>
#include <stdexcept>

namespace eloxir {

Scanner::Scanner(std::string src) : source(std::move(src)) {}

const std::unordered_map<std::string, TokenType> Scanner::keywords = {
    {"and", TokenType::AND},       {"class", TokenType::CLASS},
    {"else", TokenType::ELSE},     {"false", TokenType::FALSE},
    {"for", TokenType::FOR},       {"fun", TokenType::FUN},
    {"if", TokenType::IF},         {"nil", TokenType::NIL},
    {"or", TokenType::OR},         {"print", TokenType::PRINT},
    {"return", TokenType::RETURN}, {"super", TokenType::SUPER},
    {"this", TokenType::THIS},     {"true", TokenType::TRUE},
    {"var", TokenType::VAR},       {"while", TokenType::WHILE}};

std::vector<Token> Scanner::scanTokens() {
  while (!isAtEnd()) {
    start = current;
    scanToken();
  }
  tokens.push_back(Token{TokenType::EOF_TOKEN, "", {}, line});
  return tokens;
}

void Scanner::scanToken() {
  char c = advance();
  switch (c) {
  case '(':
    addToken(TokenType::LEFT_PAREN);
    break;
  case ')':
    addToken(TokenType::RIGHT_PAREN);
    break;
  case '{':
    addToken(TokenType::LEFT_BRACE);
    break;
  case '}':
    addToken(TokenType::RIGHT_BRACE);
    break;
  case ',':
    addToken(TokenType::COMMA);
    break;
  case '.':
    addToken(TokenType::DOT);
    break;
  case '-':
    addToken(TokenType::MINUS);
    break;
  case '+':
    addToken(TokenType::PLUS);
    break;
  case ';':
    addToken(TokenType::SEMICOLON);
    break;
  case '*':
    addToken(TokenType::STAR);
    break;
  case '!':
    addToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
    break;
  case '=':
    addToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
    break;
  case '<':
    addToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
    break;
  case '>':
    addToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
    break;
  case '/':
    if (match('/')) {
      commentLine();
    } else {
      addToken(TokenType::SLASH);
    }
    break;
  case ' ':
  case '\r':
  case '\t':
    break; // ignore whitespace
  case '\n':
    line++;
    break;
  case '"':
    string();
    break;
  default:
    if (isDigit(c)) {
      number();
    } else if (isAlpha(c)) {
      identifier();
    } else {
      throw std::runtime_error("Unexpected character at line " +
                               std::to_string(line));
    }
  }
}

char Scanner::advance() { return source[current++]; }

bool Scanner::match(char expected) {
  if (isAtEnd() || source[current] != expected)
    return false;
  current++;
  return true;
}

char Scanner::peek() const { return isAtEnd() ? '\0' : source[current]; }

char Scanner::peekNext() const {
  return (current + 1 >= source.size()) ? '\0' : source[current + 1];
}

void Scanner::addToken(TokenType type) { addToken(type, {}); }

void Scanner::addToken(TokenType type, const Literal &lit) {
  std::string text = source.substr(start, current - start);
  Token t{type, text, std::monostate{}, line};
  if (std::holds_alternative<double>(lit)) {
    t = Token{type, text, std::get<double>(lit), line};
  } else if (std::holds_alternative<std::string>(lit)) {
    t = Token{type, text, std::get<std::string>(lit), line};
  } else if (std::holds_alternative<bool>(lit)) {
    t = Token{type, text, std::get<bool>(lit), line};
  }
  tokens.push_back(std::move(t));
}

void Scanner::string() {
  std::string value;

  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\n')
      line++;

    if (peek() == '\\') {
      advance(); // consume the backslash
      if (isAtEnd()) {
        throw std::runtime_error("Unterminated string at line " +
                                 std::to_string(line));
      }

      char escaped = advance();
      switch (escaped) {
      case 'n':
        value += '\n';
        break;
      case 't':
        value += '\t';
        break;
      case 'r':
        value += '\r';
        break;
      case '\\':
        value += '\\';
        break;
      case '"':
        value += '"';
        break;
      default:
        // For unrecognized escape sequences, include the backslash
        value += '\\';
        value += escaped;
        break;
      }
    } else {
      value += advance();
    }
  }

  if (isAtEnd())
    throw std::runtime_error("Unterminated string at line " +
                             std::to_string(line));
  advance(); // closing "

  addToken(TokenType::STRING, value);
}

void Scanner::number() {
  while (isDigit(peek()))
    advance();
  if (peek() == '.' && isDigit(peekNext())) {
    advance();
    while (isDigit(peek()))
      advance();
  }
  double value = std::stod(source.substr(start, current - start));
  addToken(TokenType::NUMBER, value);
}

void Scanner::identifier() {
  while (isAlphaNumeric(peek()))
    advance();
  std::string text = source.substr(start, current - start);
  auto it = keywords.find(text);
  if (it != keywords.end())
    addToken(it->second);
  else
    addToken(TokenType::IDENTIFIER);
}

void Scanner::commentLine() {
  while (peek() != '\n' && !isAtEnd())
    advance();
}

bool Scanner::isAlpha(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool Scanner::isAlphaNumeric(char c) { return isAlpha(c) || isDigit(c); }
bool Scanner::isDigit(char c) {
  return std::isdigit(static_cast<unsigned char>(c));
}

} // namespace eloxir