#pragma once
#include "Expr.h"
#include "Stmt.h"
#include "Token.h"
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <vector>

namespace eloxir {

class Parser {
public:
  explicit Parser(const std::vector<Token> &tokens);
  std::vector<std::unique_ptr<Stmt>> parse();

  // Public for parseREPL usage
  std::unique_ptr<Expr> expression();
  bool isAtEnd();

private:
  const std::vector<Token> &tokens;
  size_t current = 0;

  // declarations
  std::unique_ptr<Stmt> declaration();
  std::unique_ptr<Stmt> classDeclaration();
  std::unique_ptr<Function> funDeclaration(const std::string &kind);
  std::unique_ptr<Stmt> varDeclaration();

  // statements
  std::unique_ptr<Stmt> statement();
  std::unique_ptr<Stmt> forStatement();
  std::unique_ptr<Stmt> ifStatement();
  std::unique_ptr<Stmt> whileStatement();
  std::unique_ptr<Stmt> printStatement();
  std::unique_ptr<Stmt> returnStatement();
  std::unique_ptr<Stmt> expressionStatement();
  std::unique_ptr<Block> block();

  // expressions
  std::unique_ptr<Expr> assignment();
  std::unique_ptr<Expr> logic_or();
  std::unique_ptr<Expr> logic_and();
  std::unique_ptr<Expr> equality();
  std::unique_ptr<Expr> comparison();
  std::unique_ptr<Expr> term();
  std::unique_ptr<Expr> factor();
  std::unique_ptr<Expr> unary();
  std::unique_ptr<Expr> call();
  std::unique_ptr<Expr> finishCall(std::unique_ptr<Expr> callee);
  std::unique_ptr<Expr> primary();

  // helpers
  bool match(const std::initializer_list<TokenType> &types);
  bool match(TokenType type);
  bool check(TokenType type);
  Token advance();
  Token peek();
  Token previous();

  Token consume(TokenType type, const std::string &message);
  std::runtime_error error(const Token &token, const std::string &message);
  void synchronize();
};

// Free function for REPL parsing
std::pair<std::unique_ptr<Stmt>, std::string>
parseREPL(const std::string &source);

} // namespace eloxir
