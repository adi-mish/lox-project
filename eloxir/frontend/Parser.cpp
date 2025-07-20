#include "Parser.h"
#include <sstream>
#include <stdexcept>

namespace eloxir {

// Simple scanner for demo purposes
std::vector<Token> scanTokens(const std::string& source) {
    std::vector<Token> tokens;
    // Just for demonstration - not a real scanner
    // In a real implementation, this would tokenize the source
    
    // Add a simple token for "print" literal
    tokens.push_back(Token(TokenType::PRINT, "print", std::monostate{}, 1));
    
    // Add string literal token
    tokens.push_back(Token(TokenType::STRING, "\"hello\"", std::string("hello"), 1));
    
    // Add semicolon
    tokens.push_back(Token(TokenType::SEMICOLON, ";", std::monostate{}, 1));
    
    // Add EOF token
    tokens.push_back(Token(TokenType::EOF_TOKEN, "", std::monostate{}, 1));
    
    return tokens;
}

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens), current(0) {}

std::vector<std::unique_ptr<Stmt::Stmt>> Parser::parse() {
    std::vector<std::unique_ptr<Stmt::Stmt>> statements;
    
    while (!isAtEnd()) {
        try {
            statements.push_back(declaration());
        } catch (const ParseError&) {
            synchronize();
        }
    }
    
    return statements;
}

std::unique_ptr<Stmt::Stmt> Parser::parseREPLLine() {
    try {
        if (match({TokenType::PRINT})) {
            return printStatement();
        }
        
        return expressionStatement();
    } catch (const ParseError&) {
        synchronize();
        return nullptr;
    }
}

std::unique_ptr<Expr::Expr> Parser::expression() {
    return assignment();
}

std::unique_ptr<Expr::Expr> Parser::assignment() {
    // Simplified assignment parsing for now
    return equality();
}

std::unique_ptr<Expr::Expr> Parser::equality() {
    // Simplified for now
    return primary();
}

std::unique_ptr<Expr::Expr> Parser::comparison() {
    // Simplified
    return term();
}

std::unique_ptr<Expr::Expr> Parser::term() {
    // Simplified
    return factor();
}

std::unique_ptr<Expr::Expr> Parser::factor() {
    // Simplified
    return unary();
}

std::unique_ptr<Expr::Expr> Parser::unary() {
    // Simplified
    return primary();
}

std::unique_ptr<Expr::Expr> Parser::primary() {
    if (match({TokenType::STRING})) {
        std::string strValue = std::get<std::string>(previous().getLiteral());
        return std::make_unique<Expr::Literal>(strValue);
    }
    
    // Simplified
    return nullptr;
}

std::unique_ptr<Expr::Expr> Parser::call() {
    // Simplified
    return primary();
}

std::unique_ptr<Expr::Expr> Parser::finishCall(std::unique_ptr<Expr::Expr> callee) {
    // Simplified
    return callee;
}

std::unique_ptr<Stmt::Stmt> Parser::declaration() {
    if (match({TokenType::VAR})) {
        return varDeclaration();
    }
    
    return statement();
}

std::unique_ptr<Stmt::Stmt> Parser::varDeclaration() {
    // Simplified
    return nullptr;
}

std::unique_ptr<Stmt::Stmt> Parser::statement() {
    if (match({TokenType::PRINT})) {
        return printStatement();
    }
    
    return expressionStatement();
}

std::unique_ptr<Stmt::Stmt> Parser::printStatement() {
    auto value = expression();
    consume(TokenType::SEMICOLON, "Expect ';' after value.");
    return std::make_unique<Stmt::Print>(std::move(value));
}

std::unique_ptr<Stmt::Stmt> Parser::expressionStatement() {
    auto expr = expression();
    consume(TokenType::SEMICOLON, "Expect ';' after expression.");
    return std::make_unique<Stmt::Expression>(std::move(expr));
}

std::unique_ptr<Stmt::Stmt> Parser::block() {
    // Simplified
    return nullptr;
}

std::unique_ptr<Stmt::Stmt> Parser::ifStatement() {
    // Simplified
    return nullptr;
}

std::unique_ptr<Stmt::Stmt> Parser::whileStatement() {
    // Simplified
    return nullptr;
}

std::unique_ptr<Stmt::Stmt> Parser::forStatement() {
    // Simplified
    return nullptr;
}

std::unique_ptr<Stmt::Function> Parser::function(const std::string& kind) {
    // Simplified
    return nullptr;
}

std::unique_ptr<Stmt::Stmt> Parser::returnStatement() {
    // Simplified
    return nullptr;
}

bool Parser::match(const std::vector<TokenType>& types) {
    for (TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

bool Parser::check(TokenType type) const {
    if (isAtEnd()) return false;
    return peek().getType() == type;
}

Token Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool Parser::isAtEnd() const {
    return peek().getType() == TokenType::EOF_TOKEN;
}

Token Parser::peek() const {
    return tokens[current];
}

Token Parser::previous() const {
    return tokens[current - 1];
}

Token Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    throw error(peek(), message);
}

Parser::ParseError Parser::error(const Token& token, const std::string& message) {
    std::ostringstream errorMsg;
    if (token.getType() == TokenType::EOF_TOKEN) {
        errorMsg << "at end: " << message;
    } else {
        errorMsg << "at '" << token.getLexeme() << "': " << message;
    }
    
    return ParseError(errorMsg.str());
}

void Parser::synchronize() {
    advance();
    
    while (!isAtEnd()) {
        if (previous().getType() == TokenType::SEMICOLON) return;
        
        switch (peek().getType()) {
            case TokenType::CLASS:
            case TokenType::FUN:
            case TokenType::VAR:
            case TokenType::FOR:
            case TokenType::IF:
            case TokenType::WHILE:
            case TokenType::PRINT:
            case TokenType::RETURN:
                return;
            default:
                break;
        }
        
        advance();
    }
}

std::pair<std::unique_ptr<Stmt::Stmt>, std::string> parseREPL(const std::string& source) {
    try {
        auto tokens = scanTokens(source);
        Parser parser(tokens);
        auto stmt = parser.parseREPLLine();
        return {std::move(stmt), ""};
    } catch (const std::exception& e) {
        return {nullptr, e.what()};
    }
}

} // namespace eloxir
