#include "parser.h"

#include <utility>

namespace loxpp {

Parser::Parser(std::vector<Token> tokens, ErrorReporter& reporter)
    : tokens_(std::move(tokens)), reporter_(reporter) {}

std::vector<StmtPtr> Parser::parse() {
  std::vector<StmtPtr> statements;
  while (!isAtEnd()) {
    statements.push_back(declaration());
  }
  return statements;
}

ExprPtr Parser::parseExpression() {
  try {
    ExprPtr expr = expression();
    if (!isAtEnd()) {
      throw error(peek(), "Expect end of expression.");
    }
    return expr;
  } catch (const ParseError&) {
    return nullptr;
  }
}

ExprPtr Parser::expression() {
  return assignment();
}

StmtPtr Parser::declaration() {
  try {
    if (match({TokenType::Class})) {
      return classDeclaration();
    }
    if (match({TokenType::Fun})) {
      return function("function");
    }
    if (match({TokenType::Var})) {
      return varDeclaration();
    }

    return statement();
  } catch (const ParseError&) {
    synchronize();
    return nullptr;
  }
}

StmtPtr Parser::classDeclaration() {
  Token name = consume(TokenType::Identifier, "Expect class name.");

  std::shared_ptr<VariableExpr> superclass;
  if (match({TokenType::Less})) {
    consume(TokenType::Identifier, "Expect superclass name.");
    superclass = std::make_shared<VariableExpr>(previous());
  }

  consume(TokenType::LeftBrace, "Expect '{' before class body.");

  std::vector<FunctionStmtPtr> methods;
  while (!check(TokenType::RightBrace) && !isAtEnd()) {
    methods.push_back(function("method"));
  }

  consume(TokenType::RightBrace, "Expect '}' after class body.");
  return std::make_shared<ClassStmt>(name, superclass, methods);
}

StmtPtr Parser::statement() {
  if (match({TokenType::For})) {
    return forStatement();
  }
  if (match({TokenType::If})) {
    return ifStatement();
  }
  if (match({TokenType::Print})) {
    return printStatement();
  }
  if (match({TokenType::Return})) {
    return returnStatement();
  }
  if (match({TokenType::While})) {
    return whileStatement();
  }
  if (match({TokenType::LeftBrace})) {
    return std::make_shared<BlockStmt>(block());
  }

  return expressionStatement();
}

StmtPtr Parser::forStatement() {
  consume(TokenType::LeftParen, "Expect '(' after 'for'.");

  StmtPtr initializer;
  if (match({TokenType::Semicolon})) {
    initializer = nullptr;
  } else if (match({TokenType::Var})) {
    initializer = varDeclaration();
  } else {
    initializer = expressionStatement();
  }

  ExprPtr condition;
  if (!check(TokenType::Semicolon)) {
    condition = expression();
  }
  consume(TokenType::Semicolon, "Expect ';' after loop condition.");

  ExprPtr increment;
  if (!check(TokenType::RightParen)) {
    increment = expression();
  }
  consume(TokenType::RightParen, "Expect ')' after for clauses.");

  StmtPtr body = statement();

  if (increment) {
    body = std::make_shared<BlockStmt>(
        std::vector<StmtPtr>{body, std::make_shared<ExpressionStmt>(increment)});
  }

  if (!condition) {
    condition = std::make_shared<LiteralExpr>(Value(true));
  }
  body = std::make_shared<WhileStmt>(condition, body);

  if (initializer) {
    body = std::make_shared<BlockStmt>(std::vector<StmtPtr>{initializer, body});
  }

  return body;
}

StmtPtr Parser::ifStatement() {
  consume(TokenType::LeftParen, "Expect '(' after 'if'.");
  ExprPtr condition = expression();
  consume(TokenType::RightParen, "Expect ')' after if condition.");

  StmtPtr then_branch = statement();
  StmtPtr else_branch;
  if (match({TokenType::Else})) {
    else_branch = statement();
  }

  return std::make_shared<IfStmt>(condition, then_branch, else_branch);
}

StmtPtr Parser::printStatement() {
  ExprPtr value = expression();
  consume(TokenType::Semicolon, "Expect ';' after value.");
  return std::make_shared<PrintStmt>(value);
}

StmtPtr Parser::returnStatement() {
  Token keyword = previous();
  ExprPtr value;
  if (!check(TokenType::Semicolon)) {
    value = expression();
  }

  consume(TokenType::Semicolon, "Expect ';' after return value.");
  return std::make_shared<ReturnStmt>(keyword, value);
}

StmtPtr Parser::varDeclaration() {
  Token name = consume(TokenType::Identifier, "Expect variable name.");

  ExprPtr initializer;
  if (match({TokenType::Equal})) {
    initializer = expression();
  }

  consume(TokenType::Semicolon, "Expect ';' after variable declaration.");
  return std::make_shared<VarStmt>(name, initializer);
}

StmtPtr Parser::whileStatement() {
  consume(TokenType::LeftParen, "Expect '(' after 'while'.");
  ExprPtr condition = expression();
  consume(TokenType::RightParen, "Expect ')' after condition.");
  StmtPtr body = statement();

  return std::make_shared<WhileStmt>(condition, body);
}

StmtPtr Parser::expressionStatement() {
  ExprPtr expr = expression();
  consume(TokenType::Semicolon, "Expect ';' after expression.");
  return std::make_shared<ExpressionStmt>(expr);
}

FunctionStmtPtr Parser::function(std::string_view kind) {
  Token name = consume(TokenType::Identifier,
                       "Expect " + std::string(kind) + " name.");
  consume(TokenType::LeftParen,
          "Expect '(' after " + std::string(kind) + " name.");
  std::vector<Token> parameters;
  if (!check(TokenType::RightParen)) {
    do {
      if (parameters.size() >= 255) {
        error(peek(), "Can't have more than 255 parameters.");
      }
      parameters.push_back(
          consume(TokenType::Identifier, "Expect parameter name."));
    } while (match({TokenType::Comma}));
  }
  consume(TokenType::RightParen, "Expect ')' after parameters.");

  consume(TokenType::LeftBrace,
          "Expect '{' before " + std::string(kind) + " body.");
  return std::make_shared<FunctionStmt>(name, parameters, block());
}

std::vector<StmtPtr> Parser::block() {
  std::vector<StmtPtr> statements;

  while (!check(TokenType::RightBrace) && !isAtEnd()) {
    statements.push_back(declaration());
  }

  consume(TokenType::RightBrace, "Expect '}' after block.");
  return statements;
}

ExprPtr Parser::assignment() {
  ExprPtr expr = orExpression();

  if (match({TokenType::Equal})) {
    Token equals = previous();
    ExprPtr value = assignment();

    if (auto variable = std::dynamic_pointer_cast<VariableExpr>(expr)) {
      return std::make_shared<AssignExpr>(variable->name, value);
    }
    if (auto get = std::dynamic_pointer_cast<GetExpr>(expr)) {
      return std::make_shared<SetExpr>(get->object, get->name, value);
    }

    error(equals, "Invalid assignment target.");
  }

  return expr;
}

ExprPtr Parser::orExpression() {
  ExprPtr expr = andExpression();

  while (match({TokenType::Or})) {
    Token op = previous();
    ExprPtr right = andExpression();
    expr = std::make_shared<LogicalExpr>(expr, op, right);
  }

  return expr;
}

ExprPtr Parser::andExpression() {
  ExprPtr expr = equality();

  while (match({TokenType::And})) {
    Token op = previous();
    ExprPtr right = equality();
    expr = std::make_shared<LogicalExpr>(expr, op, right);
  }

  return expr;
}

ExprPtr Parser::equality() {
  ExprPtr expr = comparison();

  while (match({TokenType::BangEqual, TokenType::EqualEqual})) {
    Token op = previous();
    ExprPtr right = comparison();
    expr = std::make_shared<BinaryExpr>(expr, op, right);
  }

  return expr;
}

ExprPtr Parser::comparison() {
  ExprPtr expr = term();

  while (match({TokenType::Greater, TokenType::GreaterEqual, TokenType::Less,
                TokenType::LessEqual})) {
    Token op = previous();
    ExprPtr right = term();
    expr = std::make_shared<BinaryExpr>(expr, op, right);
  }

  return expr;
}

ExprPtr Parser::term() {
  ExprPtr expr = factor();

  while (match({TokenType::Minus, TokenType::Plus})) {
    Token op = previous();
    ExprPtr right = factor();
    expr = std::make_shared<BinaryExpr>(expr, op, right);
  }

  return expr;
}

ExprPtr Parser::factor() {
  ExprPtr expr = unary();

  while (match({TokenType::Slash, TokenType::Star})) {
    Token op = previous();
    ExprPtr right = unary();
    expr = std::make_shared<BinaryExpr>(expr, op, right);
  }

  return expr;
}

ExprPtr Parser::unary() {
  if (match({TokenType::Bang, TokenType::Minus})) {
    Token op = previous();
    ExprPtr right = unary();
    return std::make_shared<UnaryExpr>(op, right);
  }

  return call();
}

ExprPtr Parser::finishCall(ExprPtr callee) {
  std::vector<ExprPtr> arguments;
  if (!check(TokenType::RightParen)) {
    do {
      if (arguments.size() >= 255) {
        error(peek(), "Can't have more than 255 arguments.");
      }
      arguments.push_back(expression());
    } while (match({TokenType::Comma}));
  }

  Token paren = consume(TokenType::RightParen, "Expect ')' after arguments.");
  return std::make_shared<CallExpr>(callee, paren, arguments);
}

ExprPtr Parser::call() {
  ExprPtr expr = primary();

  while (true) {
    if (match({TokenType::LeftParen})) {
      expr = finishCall(expr);
    } else if (match({TokenType::Dot})) {
      Token name =
          consume(TokenType::Identifier, "Expect property name after '.'.");
      expr = std::make_shared<GetExpr>(expr, name);
    } else {
      break;
    }
  }

  return expr;
}

ExprPtr Parser::primary() {
  if (match({TokenType::False})) {
    return std::make_shared<LiteralExpr>(Value(false));
  }
  if (match({TokenType::True})) {
    return std::make_shared<LiteralExpr>(Value(true));
  }
  if (match({TokenType::Nil})) {
    return std::make_shared<LiteralExpr>(Value(std::monostate{}));
  }

  if (match({TokenType::Number})) {
    return std::make_shared<LiteralExpr>(
        Value(std::get<double>(previous().literal)));
  }
  if (match({TokenType::String})) {
    return std::make_shared<LiteralExpr>(
        Value(std::get<std::string>(previous().literal)));
  }

  if (match({TokenType::Super})) {
    Token keyword = previous();
    consume(TokenType::Dot, "Expect '.' after 'super'.");
    Token method =
        consume(TokenType::Identifier, "Expect superclass method name.");
    return std::make_shared<SuperExpr>(keyword, method);
  }

  if (match({TokenType::This})) {
    return std::make_shared<ThisExpr>(previous());
  }

  if (match({TokenType::Identifier})) {
    return std::make_shared<VariableExpr>(previous());
  }

  if (match({TokenType::LeftParen})) {
    ExprPtr expr = expression();
    consume(TokenType::RightParen, "Expect ')' after expression.");
    return std::make_shared<GroupingExpr>(expr);
  }

  throw error(peek(), "Expect expression.");
}

bool Parser::match(std::initializer_list<TokenType> types) {
  for (const TokenType type : types) {
    if (check(type)) {
      advance();
      return true;
    }
  }
  return false;
}

Token Parser::consume(TokenType type, std::string_view message) {
  if (check(type)) {
    return advance();
  }

  throw error(peek(), message);
}

bool Parser::check(TokenType type) const {
  if (isAtEnd()) {
    return false;
  }
  return peek().type == type;
}

Token Parser::advance() {
  if (!isAtEnd()) {
    ++current_;
  }
  return previous();
}

bool Parser::isAtEnd() const {
  return peek().type == TokenType::Eof;
}

const Token& Parser::peek() const {
  return tokens_[current_];
}

const Token& Parser::previous() const {
  return tokens_[current_ - 1];
}

Parser::ParseError Parser::error(const Token& token, std::string_view message) {
  reporter_.error(token, message);
  return ParseError();
}

void Parser::synchronize() {
  advance();

  while (!isAtEnd()) {
    if (previous().type == TokenType::Semicolon) {
      return;
    }

    switch (peek().type) {
      case TokenType::Class:
      case TokenType::Fun:
      case TokenType::Var:
      case TokenType::For:
      case TokenType::If:
      case TokenType::While:
      case TokenType::Print:
      case TokenType::Return:
        return;
      default:
        break;
    }

    advance();
  }
}

}  // namespace loxpp
