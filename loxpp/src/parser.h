#pragma once

#include <initializer_list>
#include <string_view>
#include <vector>

#include "error_reporter.h"
#include "expr.h"
#include "stmt.h"
#include "token.h"

namespace loxpp {

class Parser {
 public:
  Parser(std::vector<Token> tokens, ErrorReporter& reporter);

  std::vector<StmtPtr> parse();
  ExprPtr parseExpression();

 private:
  class ParseError {};

  ExprPtr expression();
  StmtPtr declaration();
  StmtPtr classDeclaration();
  StmtPtr statement();
  StmtPtr forStatement();
  StmtPtr ifStatement();
  StmtPtr printStatement();
  StmtPtr returnStatement();
  StmtPtr varDeclaration();
  StmtPtr whileStatement();
  StmtPtr expressionStatement();
  FunctionStmtPtr function(std::string_view kind);
  std::vector<StmtPtr> block();

  ExprPtr assignment();
  ExprPtr orExpression();
  ExprPtr andExpression();
  ExprPtr equality();
  ExprPtr comparison();
  ExprPtr term();
  ExprPtr factor();
  ExprPtr unary();
  ExprPtr finishCall(ExprPtr callee);
  ExprPtr call();
  ExprPtr primary();

  bool match(std::initializer_list<TokenType> types);
  Token consume(TokenType type, std::string_view message);
  [[nodiscard]] bool check(TokenType type) const;
  Token advance();
  [[nodiscard]] bool isAtEnd() const;
  [[nodiscard]] const Token& peek() const;
  [[nodiscard]] const Token& previous() const;
  ParseError error(const Token& token, std::string_view message);
  void synchronize();

  std::vector<Token> tokens_;
  ErrorReporter& reporter_;
  std::size_t current_ = 0;
};

}  // namespace loxpp
