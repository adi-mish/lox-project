#pragma once
#include <string>
#include <variant>

namespace eloxir {

enum class TokenType {
    // Single-character tokens
    LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE,
    COMMA, DOT, MINUS, PLUS, SEMICOLON, SLASH, STAR,

    // One or two character tokens
    BANG, BANG_EQUAL,
    EQUAL, EQUAL_EQUAL,
    GREATER, GREATER_EQUAL,
    LESS, LESS_EQUAL,

    // Literals
    IDENTIFIER, STRING, NUMBER,

    // Keywords
    AND, CLASS, ELSE, FALSE, FUN, FOR, IF, NIL, OR,
    PRINT, RETURN, SUPER, THIS, TRUE, VAR, WHILE,

    EOF_TOKEN
};

class Token {
    TokenType type;
    std::string lexeme;
    std::variant<std::monostate, double, std::string, bool> literal;
    int line;

public:
    Token(TokenType type, std::string lexeme, 
          std::variant<std::monostate, double, std::string, bool> literal, 
          int line)
        : type(type), lexeme(std::move(lexeme)), literal(std::move(literal)), line(line) {}

    TokenType getType() const { return type; }
    const std::string& getLexeme() const { return lexeme; }
    const std::variant<std::monostate, double, std::string, bool>& getLiteral() const { return literal; }
    int getLine() const { return line; }
};

} // namespace eloxir
