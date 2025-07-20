#pragma once
#include "Token.h"
#include "Expr.h"
#include "Stmt.h"
#include <vector>
#include <string>
#include <memory>
#include <utility>

namespace eloxir {

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);
    
    std::vector<std::unique_ptr<Stmt::Stmt>> parse();
    std::unique_ptr<Stmt::Stmt> parseREPLLine();

private:
    const std::vector<Token>& tokens;
    int current = 0;
    
    std::unique_ptr<Expr::Expr> expression();
    std::unique_ptr<Expr::Expr> assignment();
    std::unique_ptr<Expr::Expr> equality();
    std::unique_ptr<Expr::Expr> comparison();
    std::unique_ptr<Expr::Expr> term();
    std::unique_ptr<Expr::Expr> factor();
    std::unique_ptr<Expr::Expr> unary();
    std::unique_ptr<Expr::Expr> primary();
    std::unique_ptr<Expr::Expr> call();
    std::unique_ptr<Expr::Expr> finishCall(std::unique_ptr<Expr::Expr> callee);

    std::unique_ptr<Stmt::Stmt> declaration();
    std::unique_ptr<Stmt::Stmt> varDeclaration();
    std::unique_ptr<Stmt::Stmt> statement();
    std::unique_ptr<Stmt::Stmt> printStatement();
    std::unique_ptr<Stmt::Stmt> expressionStatement();
    std::unique_ptr<Stmt::Stmt> block();
    std::unique_ptr<Stmt::Stmt> ifStatement();
    std::unique_ptr<Stmt::Stmt> whileStatement();
    std::unique_ptr<Stmt::Stmt> forStatement();
    std::unique_ptr<Stmt::Function> function(const std::string& kind);
    std::unique_ptr<Stmt::Stmt> returnStatement();

    bool match(const std::vector<TokenType>& types);
    bool check(TokenType type) const;
    Token advance();
    bool isAtEnd() const;
    Token peek() const;
    Token previous() const;
    Token consume(TokenType type, const std::string& message);
    
    class ParseError : public std::runtime_error {
    public:
        explicit ParseError(const std::string& what) : std::runtime_error(what) {}
    };
    
    ParseError error(const Token& token, const std::string& message);
    void synchronize();
};

std::pair<std::unique_ptr<Stmt::Stmt>, std::string> parseREPL(const std::string& source);

} // namespace eloxir
