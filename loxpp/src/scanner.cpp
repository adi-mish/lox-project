#include "scanner.h"

#include <charconv>
#include <cstdlib>
#include <unordered_map>

namespace loxpp {

namespace {

const std::unordered_map<std::string_view, TokenType> kKeywords = {
    {"and", TokenType::And},     {"class", TokenType::Class},
    {"else", TokenType::Else},   {"false", TokenType::False},
    {"for", TokenType::For},     {"fun", TokenType::Fun},
    {"if", TokenType::If},       {"nil", TokenType::Nil},
    {"or", TokenType::Or},       {"print", TokenType::Print},
    {"return", TokenType::Return},
    {"super", TokenType::Super}, {"this", TokenType::This},
    {"true", TokenType::True},   {"var", TokenType::Var},
    {"while", TokenType::While},
};

}  // namespace

Scanner::Scanner(std::string source, ErrorReporter& reporter)
    : source_(std::move(source)), reporter_(reporter) {}

std::vector<Token> Scanner::scanTokens() {
  while (!isAtEnd()) {
    start_ = current_;
    scanToken();
  }

  tokens_.push_back(Token{TokenType::Eof, "", std::monostate{}, line_});
  return tokens_;
}

bool Scanner::isAtEnd() const {
  return current_ >= source_.size();
}

char Scanner::advance() {
  return source_[current_++];
}

void Scanner::addToken(TokenType type) {
  addToken(type, std::monostate{});
}

void Scanner::addToken(TokenType type, TokenLiteral literal) {
  tokens_.push_back(Token{
      type,
      source_.substr(start_, current_ - start_),
      std::move(literal),
      line_,
  });
}

bool Scanner::match(char expected) {
  if (isAtEnd()) {
    return false;
  }
  if (source_[current_] != expected) {
    return false;
  }

  ++current_;
  return true;
}

char Scanner::peek() const {
  if (isAtEnd()) {
    return '\0';
  }
  return source_[current_];
}

char Scanner::peekNext() const {
  if (current_ + 1 >= source_.size()) {
    return '\0';
  }
  return source_[current_ + 1];
}

void Scanner::scanToken() {
  const char c = advance();
  switch (c) {
    case '(':
      addToken(TokenType::LeftParen);
      break;
    case ')':
      addToken(TokenType::RightParen);
      break;
    case '{':
      addToken(TokenType::LeftBrace);
      break;
    case '}':
      addToken(TokenType::RightBrace);
      break;
    case ',':
      addToken(TokenType::Comma);
      break;
    case '.':
      addToken(TokenType::Dot);
      break;
    case '-':
      addToken(TokenType::Minus);
      break;
    case '+':
      addToken(TokenType::Plus);
      break;
    case ';':
      addToken(TokenType::Semicolon);
      break;
    case '*':
      addToken(TokenType::Star);
      break;
    case '!':
      addToken(match('=') ? TokenType::BangEqual : TokenType::Bang);
      break;
    case '=':
      addToken(match('=') ? TokenType::EqualEqual : TokenType::Equal);
      break;
    case '<':
      addToken(match('=') ? TokenType::LessEqual : TokenType::Less);
      break;
    case '>':
      addToken(match('=') ? TokenType::GreaterEqual : TokenType::Greater);
      break;
    case '/':
      if (match('/')) {
        while (peek() != '\n' && !isAtEnd()) {
          advance();
        }
      } else {
        addToken(TokenType::Slash);
      }
      break;
    case '"':
      string();
      break;
    case ' ':
    case '\r':
    case '\t':
      break;
    case '\n':
      ++line_;
      break;
    default:
      if (isDigit(c)) {
        number();
      } else if (isAlpha(c)) {
        identifier();
      } else {
        reporter_.error(line_, "Unexpected character.");
      }
      break;
  }
}

void Scanner::identifier() {
  while (isAlphaNumeric(peek())) {
    advance();
  }

  const std::string_view text(source_.data() + start_, current_ - start_);
  if (const auto it = kKeywords.find(text); it != kKeywords.end()) {
    addToken(it->second);
  } else {
    addToken(TokenType::Identifier);
  }
}

void Scanner::string() {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\n') {
      ++line_;
    }
    advance();
  }

  if (isAtEnd()) {
    reporter_.error(line_, "Unterminated string.");
    return;
  }

  advance();
  addToken(TokenType::String, source_.substr(start_ + 1, current_ - start_ - 2));
}

void Scanner::number() {
  while (isDigit(peek())) {
    advance();
  }

  if (peek() == '.' && isDigit(peekNext())) {
    advance();

    while (isDigit(peek())) {
      advance();
    }
  }

  addToken(TokenType::Number,
           std::strtod(source_.c_str() + start_, nullptr));
}

bool Scanner::isDigit(char c) {
  return c >= '0' && c <= '9';
}

bool Scanner::isAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool Scanner::isAlphaNumeric(char c) {
  return isAlpha(c) || isDigit(c);
}

}  // namespace loxpp
