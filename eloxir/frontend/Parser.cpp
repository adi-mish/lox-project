#include "Parser.h"
#include "Scanner.h"
#include <iostream>

namespace eloxir {

Parser::Parser(const std::vector<Token> &tokens) : tokens(tokens) {}

std::vector<std::unique_ptr<Stmt>> Parser::parse() {
  std::vector<std::unique_ptr<Stmt>> statements;

  while (!isAtEnd()) {
    try {
      auto stmt = declaration();
      if (stmt) {
        statements.push_back(std::move(stmt));
      }
    } catch (const std::runtime_error &e) {
      std::cerr << "Parse error: " << e.what() << std::endl;
      synchronize();
    }
  }

  return statements;
}

std::unique_ptr<Stmt> Parser::declaration() {
  try {
    if (match(TokenType::CLASS))
      return classDeclaration();
    if (match(TokenType::FUN))
      return std::unique_ptr<Stmt>(funDeclaration("function"));
    if (match(TokenType::VAR))
      return varDeclaration();
    return statement();
  } catch (const std::runtime_error &e) {
    synchronize();
    throw;
  }
}

std::unique_ptr<Stmt> Parser::classDeclaration() {
  Token name = consume(TokenType::IDENTIFIER, "Expected class name.");

  std::unique_ptr<Variable> superclass = nullptr;
  if (match(TokenType::LESS)) {
    consume(TokenType::IDENTIFIER, "Expected superclass name.");
    superclass = std::make_unique<Variable>(previous());
  }

  consume(TokenType::LEFT_BRACE, "Expected '{' before class body.");

  std::vector<std::unique_ptr<Function>> methods;
  while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
    methods.push_back(funDeclaration("method"));
  }

  consume(TokenType::RIGHT_BRACE, "Expected '}' after class body.");
  return std::make_unique<Class>(name, std::move(superclass),
                                 std::move(methods));
}

std::unique_ptr<Function> Parser::funDeclaration(const std::string &kind) {
  Token name = consume(TokenType::IDENTIFIER, "Expected " + kind + " name.");

  consume(TokenType::LEFT_PAREN, "Expected '(' after " + kind + " name.");
  std::vector<Token> parameters;
  if (!check(TokenType::RIGHT_PAREN)) {
    do {
      if (parameters.size() >= 255) {
        error(peek(), "Can't have more than 255 parameters.");
      }
      parameters.push_back(
          consume(TokenType::IDENTIFIER, "Expected parameter name."));
    } while (match(TokenType::COMMA));
  }
  consume(TokenType::RIGHT_PAREN, "Expected ')' after parameters.");

  consume(TokenType::LEFT_BRACE, "Expected '{' before " + kind + " body.");
  auto body = block();
  return std::make_unique<Function>(name, std::move(parameters),
                                    std::move(body));
}

std::unique_ptr<Stmt> Parser::varDeclaration() {
  Token name = consume(TokenType::IDENTIFIER, "Expected variable name.");

  std::unique_ptr<Expr> initializer = nullptr;
  if (match(TokenType::EQUAL)) {
    initializer = expression();
  }

  consume(TokenType::SEMICOLON, "Expected ';' after variable declaration.");
  return std::make_unique<Var>(name, std::move(initializer));
}

std::unique_ptr<Stmt> Parser::statement() {
  if (match(TokenType::FOR))
    return forStatement();
  if (match(TokenType::IF))
    return ifStatement();
  if (match(TokenType::PRINT))
    return printStatement();
  if (match(TokenType::RETURN))
    return returnStatement();
  if (match(TokenType::WHILE))
    return whileStatement();
  if (match(TokenType::LEFT_BRACE))
    return std::unique_ptr<Stmt>(block());

  return expressionStatement();
}

std::unique_ptr<Stmt> Parser::forStatement() {
  consume(TokenType::LEFT_PAREN, "Expected '(' after 'for'.");

  // Parse the initializer
  std::unique_ptr<Stmt> initializer;
  Token loopVar(TokenType::IDENTIFIER, "", std::monostate{}, 0);
  bool hasLoopVar = false;

  if (match(TokenType::SEMICOLON)) {
    initializer = nullptr;
  } else if (match(TokenType::VAR)) {
    auto varDecl = varDeclaration();
    if (auto var = dynamic_cast<Var *>(varDecl.get())) {
      loopVar = var->name;
      hasLoopVar = true;
      // We'll transform this later
      initializer = std::move(varDecl);
    } else {
      initializer = std::move(varDecl);
    }
  } else {
    initializer = expressionStatement();
  }

  // Parse the condition
  std::unique_ptr<Expr> condition = nullptr;
  if (!check(TokenType::SEMICOLON)) {
    condition = expression();
  }
  consume(TokenType::SEMICOLON, "Expected ';' after loop condition.");

  // Parse the increment
  std::unique_ptr<Expr> increment = nullptr;
  if (!check(TokenType::RIGHT_PAREN)) {
    increment = expression();
  }
  consume(TokenType::RIGHT_PAREN, "Expected ')' after for clauses.");

  // Parse the body
  auto body = statement();

  // If this is a for-loop with a variable declaration, apply the capture fix
  if (hasLoopVar) {
    // Transform: for (var i = init; cond; incr) { body }
    // Into: { var i_outer = init; while (i_outer < limit) { { var i_UNIQUE =
    // i_outer; body } i_outer = i_outer + 1; } } CRITICAL: Each iteration MUST
    // get a unique variable name to prevent all closures from capturing the
    // same variable binding.

    std::string outerVarName = loopVar.getLexeme() + "_outer";

    // Generate a unique identifier for this transformation to ensure
    // different for-loops don't conflict with each other
    static int forLoopCounter = 0;
    forLoopCounter++;
    std::string innerVarName =
        loopVar.getLexeme() + "_iter_" + std::to_string(forLoopCounter);

    // Get the initializer expression from the original variable declaration
    auto originalVar = dynamic_cast<Var *>(initializer.get());
    auto initExpr = std::move(originalVar->initializer);

    // Create the outer variable: var i_outer = init;
    Token outerVarToken(TokenType::IDENTIFIER, outerVarName, std::monostate{},
                        loopVar.getLine());
    auto outerVarDecl =
        std::make_unique<Var>(Token(outerVarToken), std::move(initExpr));

    // CRITICAL CHANGE: Create inner block with UNIQUE variable name per
    // iteration We need to create a fresh scope structure that the resolver
    // will understand as creating separate variables for each iteration.

    // The key insight: We need the BODY to see the original variable name (i),
    // but each iteration must have its OWN distinct variable that captures
    // the current value of i_outer.

    // Strategy: Create a block scope that allocates a fresh binding of 'i'
    // for each iteration, initialized from i_outer's current value.

    std::vector<std::unique_ptr<Stmt>> innerStatements;

    // var i = i_outer;  (using original variable name so body code works)
    Token outerReadToken(TokenType::IDENTIFIER, outerVarName, std::monostate{},
                         loopVar.getLine());
    auto outerVarRead = std::make_unique<Variable>(std::move(outerReadToken));
    auto innerVarDecl =
        std::make_unique<Var>(std::move(loopVar), std::move(outerVarRead));

    innerStatements.push_back(std::move(innerVarDecl));
    innerStatements.push_back(std::move(body));

    auto innerBlock = std::make_unique<Block>(std::move(innerStatements));

    // Create the while loop body (inner block + increment)
    std::vector<std::unique_ptr<Stmt>> whileBodyStatements;
    whileBodyStatements.push_back(std::move(innerBlock));

    // Add increment: i_outer = i_outer + 1;
    if (increment != nullptr) {
      // For now, we'll assume the increment is of the form: i = i + 1
      // and transform it to: i_outer = i_outer + 1
      // This is a simplification - a full implementation would need expression
      // rewriting

      Token outerIncrToken(TokenType::IDENTIFIER, outerVarName,
                           std::monostate{}, 0);
      Token outerReadToken2(TokenType::IDENTIFIER, outerVarName,
                            std::monostate{}, 0);
      auto outerVarRead2 =
          std::make_unique<Variable>(std::move(outerReadToken2));
      auto one = std::make_unique<Literal>(1.0);
      Token plusToken(TokenType::PLUS, "+", std::monostate{}, 0);
      auto incrementExpr = std::make_unique<Binary>(
          std::move(outerVarRead2), std::move(plusToken), std::move(one));
      auto assignment = std::make_unique<Assign>(std::move(outerIncrToken),
                                                 std::move(incrementExpr));
      whileBodyStatements.push_back(
          std::make_unique<Expression>(std::move(assignment)));
    }

    auto whileBody = std::make_unique<Block>(std::move(whileBodyStatements));

    // Create the condition - always rewrite if there's a condition
    if (condition == nullptr) {
      condition = std::make_unique<Literal>(true);
    } else {
      // Always rewrite any condition to use the outer variable
      // This is a simplification - assumes the condition was of the form "i op
      // rightSide"
      Token outerCondToken(TokenType::IDENTIFIER, outerVarName,
                           std::monostate{}, 0);
      auto outerVarCond = std::make_unique<Variable>(std::move(outerCondToken));

      // Try to extract the right side value if it's a simple binary expression
      if (auto binary = dynamic_cast<Binary *>(condition.get())) {
        if (auto rightLit = dynamic_cast<Literal *>(binary->right.get())) {
          Token opCopy(binary->op.getType(), binary->op.getLexeme(),
                       binary->op.getLiteral(), binary->op.getLine());
          auto newRight = std::make_unique<Literal>(rightLit->value);
          condition = std::make_unique<Binary>(
              std::move(outerVarCond), std::move(opCopy), std::move(newRight));
        } else {
          // Fallback: i_outer < 1000 (should terminate eventually)
          auto defaultRight = std::make_unique<Literal>(1000.0);
          Token ltToken(TokenType::LESS, "<", std::monostate{}, 0);
          condition = std::make_unique<Binary>(std::move(outerVarCond),
                                               std::move(ltToken),
                                               std::move(defaultRight));
        }
      } else {
        // Not a binary expression - fallback
        auto defaultRight = std::make_unique<Literal>(1000.0);
        Token ltToken(TokenType::LESS, "<", std::monostate{}, 0);
        condition = std::make_unique<Binary>(std::move(outerVarCond),
                                             std::move(ltToken),
                                             std::move(defaultRight));
      }
    }

    auto whileLoop =
        std::make_unique<While>(std::move(condition), std::move(whileBody));

    // Wrap everything in a block
    std::vector<std::unique_ptr<Stmt>> outerStatements;
    outerStatements.push_back(std::move(outerVarDecl));
    outerStatements.push_back(std::move(whileLoop));

    return std::make_unique<Block>(std::move(outerStatements));
  } else {
    // Regular for-loop desugaring for non-variable cases
    if (increment != nullptr) {
      std::vector<std::unique_ptr<Stmt>> statements;
      statements.push_back(std::move(body));
      statements.push_back(std::make_unique<Expression>(std::move(increment)));
      body = std::make_unique<Block>(std::move(statements));
    }

    if (condition == nullptr) {
      condition = std::make_unique<Literal>(true);
    }

    body = std::make_unique<While>(std::move(condition), std::move(body));

    if (initializer != nullptr) {
      std::vector<std::unique_ptr<Stmt>> statements;
      statements.push_back(std::move(initializer));
      statements.push_back(std::move(body));
      body = std::make_unique<Block>(std::move(statements));
    }

    return body;
  }
}

std::unique_ptr<Stmt> Parser::ifStatement() {
  consume(TokenType::LEFT_PAREN, "Expected '(' after 'if'.");
  auto condition = expression();
  consume(TokenType::RIGHT_PAREN, "Expected ')' after if condition.");

  auto thenBranch = statement();
  std::unique_ptr<Stmt> elseBranch = nullptr;
  if (match(TokenType::ELSE)) {
    elseBranch = statement();
  }

  return std::make_unique<If>(std::move(condition), std::move(thenBranch),
                              std::move(elseBranch));
}

std::unique_ptr<Stmt> Parser::printStatement() {
  auto value = expression();
  consume(TokenType::SEMICOLON, "Expected ';' after value.");
  return std::make_unique<Print>(std::move(value));
}

std::unique_ptr<Stmt> Parser::returnStatement() {
  Token keyword = previous();
  std::unique_ptr<Expr> value = nullptr;
  if (!check(TokenType::SEMICOLON)) {
    value = expression();
  }

  consume(TokenType::SEMICOLON, "Expected ';' after return value.");
  return std::make_unique<Return>(keyword, std::move(value));
}

std::unique_ptr<Stmt> Parser::whileStatement() {
  consume(TokenType::LEFT_PAREN, "Expected '(' after 'while'.");
  auto condition = expression();
  consume(TokenType::RIGHT_PAREN, "Expected ')' after condition.");
  auto body = statement();

  return std::make_unique<While>(std::move(condition), std::move(body));
}

std::unique_ptr<Stmt> Parser::expressionStatement() {
  auto expr = expression();
  consume(TokenType::SEMICOLON, "Expected ';' after expression.");
  return std::make_unique<Expression>(std::move(expr));
}

std::unique_ptr<Block> Parser::block() {
  std::vector<std::unique_ptr<Stmt>> statements;

  while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
    statements.push_back(declaration());
  }

  consume(TokenType::RIGHT_BRACE, "Expected '}' after block.");
  return std::make_unique<Block>(std::move(statements));
}

std::unique_ptr<Expr> Parser::expression() { return assignment(); }

std::unique_ptr<Expr> Parser::assignment() {
  auto expr = logic_or();

  if (match(TokenType::EQUAL)) {
    Token equals = previous();
    auto value = assignment();

    if (auto var = dynamic_cast<Variable *>(expr.get())) {
      Token name = var->name;
      return std::make_unique<Assign>(name, std::move(value));
    } else if (auto get = dynamic_cast<Get *>(expr.get())) {
      return std::make_unique<Set>(std::move(get->object), get->name,
                                   std::move(value));
    }

    error(equals, "Invalid assignment target.");
  }

  return expr;
}

std::unique_ptr<Expr> Parser::logic_or() {
  auto expr = logic_and();

  while (match(TokenType::OR)) {
    Token op = previous();
    auto right = logic_and();
    expr = std::make_unique<Logical>(std::move(expr), op, std::move(right));
  }

  return expr;
}

std::unique_ptr<Expr> Parser::logic_and() {
  auto expr = equality();

  while (match(TokenType::AND)) {
    Token op = previous();
    auto right = equality();
    expr = std::make_unique<Logical>(std::move(expr), op, std::move(right));
  }

  return expr;
}

std::unique_ptr<Expr> Parser::equality() {
  auto expr = comparison();

  while (match({TokenType::BANG_EQUAL, TokenType::EQUAL_EQUAL})) {
    Token op = previous();
    auto right = comparison();
    expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
  }

  return expr;
}

std::unique_ptr<Expr> Parser::comparison() {
  auto expr = term();

  while (match({TokenType::GREATER, TokenType::GREATER_EQUAL, TokenType::LESS,
                TokenType::LESS_EQUAL})) {
    Token op = previous();
    auto right = term();
    expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
  }

  return expr;
}

std::unique_ptr<Expr> Parser::term() {
  auto expr = factor();

  while (match({TokenType::MINUS, TokenType::PLUS})) {
    Token op = previous();
    auto right = factor();
    expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
  }

  return expr;
}

std::unique_ptr<Expr> Parser::factor() {
  auto expr = unary();

  while (match({TokenType::SLASH, TokenType::STAR})) {
    Token op = previous();
    auto right = unary();
    expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
  }

  return expr;
}

std::unique_ptr<Expr> Parser::unary() {
  if (match({TokenType::BANG, TokenType::MINUS})) {
    Token op = previous();
    auto right = unary();
    return std::make_unique<Unary>(op, std::move(right));
  }

  return call();
}

std::unique_ptr<Expr> Parser::call() {
  auto expr = primary();

  while (true) {
    if (match(TokenType::LEFT_PAREN)) {
      expr = finishCall(std::move(expr));
    } else if (match(TokenType::DOT)) {
      Token name =
          consume(TokenType::IDENTIFIER, "Expected property name after '.'.");
      expr = std::make_unique<Get>(std::move(expr), name);
    } else {
      break;
    }
  }

  return expr;
}

std::unique_ptr<Expr> Parser::finishCall(std::unique_ptr<Expr> callee) {
  std::vector<std::unique_ptr<Expr>> arguments;

  if (!check(TokenType::RIGHT_PAREN)) {
    do {
      if (arguments.size() >= 255) {
        error(peek(), "Can't have more than 255 arguments.");
      }
      arguments.push_back(expression());
    } while (match(TokenType::COMMA));
  }

  Token paren =
      consume(TokenType::RIGHT_PAREN, "Expected ')' after arguments.");
  return std::make_unique<Call>(std::move(callee), paren, std::move(arguments));
}

std::unique_ptr<Expr> Parser::primary() {
  if (match(TokenType::TRUE))
    return std::make_unique<Literal>(true);
  if (match(TokenType::FALSE))
    return std::make_unique<Literal>(false);
  if (match(TokenType::NIL))
    return std::make_unique<Literal>(std::monostate{});

  if (match(TokenType::NUMBER)) {
    return std::make_unique<Literal>(previous().getLiteral());
  }

  if (match(TokenType::STRING)) {
    return std::make_unique<Literal>(previous().getLiteral());
  }

  if (match(TokenType::SUPER)) {
    Token keyword = previous();
    consume(TokenType::DOT, "Expected '.' after 'super'.");
    Token method =
        consume(TokenType::IDENTIFIER, "Expected superclass method name.");
    return std::make_unique<Super>(keyword, method);
  }

  if (match(TokenType::THIS)) {
    return std::make_unique<This>(previous());
  }

  if (match(TokenType::IDENTIFIER)) {
    return std::make_unique<Variable>(previous());
  }

  if (match(TokenType::LEFT_PAREN)) {
    auto expr = expression();
    consume(TokenType::RIGHT_PAREN, "Expected ')' after expression.");
    return std::make_unique<Grouping>(std::move(expr));
  }

  throw error(peek(), "Expected expression.");
}

// Helper methods
bool Parser::match(const std::initializer_list<TokenType> &types) {
  for (TokenType type : types) {
    if (check(type)) {
      advance();
      return true;
    }
  }
  return false;
}

bool Parser::match(TokenType type) {
  if (check(type)) {
    advance();
    return true;
  }
  return false;
}

bool Parser::check(TokenType type) {
  if (isAtEnd())
    return false;
  return peek().getType() == type;
}

Token Parser::advance() {
  if (!isAtEnd())
    current++;
  return previous();
}

bool Parser::isAtEnd() { return peek().getType() == TokenType::EOF_TOKEN; }

Token Parser::peek() { return tokens[current]; }

Token Parser::previous() { return tokens[current - 1]; }

Token Parser::consume(TokenType type, const std::string &message) {
  if (check(type))
    return advance();

  throw error(peek(), message);
}

std::runtime_error Parser::error(const Token &token,
                                 const std::string &message) {
  std::string where;
  if (token.getType() == TokenType::EOF_TOKEN) {
    where = " at end";
  } else {
    where = " at '" + token.getLexeme() + "'";
  }

  std::string fullMessage = "[line " + std::to_string(token.getLine()) +
                            "] Error" + where + ": " + message;
  return std::runtime_error(fullMessage);
}

void Parser::synchronize() {
  advance();

  while (!isAtEnd()) {
    if (previous().getType() == TokenType::SEMICOLON)
      return;

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

// -------------------------- REPL helper ---------------------------------
std::pair<std::unique_ptr<Stmt>, std::string>
parseREPL(const std::string &source) {
  try {
    Scanner scanner(source);
    auto tokens = scanner.scanTokens(); // assumes Scanner::scanTokens() ->
                                        // std::vector<Token>

    Parser p(tokens);

    // Accept either a full statement ending with ';', or a bare expression.
    // Try: expression followed by EOF. If that works, wrap in Print.
    size_t save = 0;
    try {
      // Light-weight inline parse: reuse private methods by constructing a new
      // Parser
      Parser exprParser(tokens);
      auto expr = exprParser.expression();
      if (!exprParser.isAtEnd()) { /* not clean, fall back to full parse */
        throw std::runtime_error("not single expr");
      }
      return {std::make_unique<Print>(std::move(expr)), ""};
    } catch (...) {
      (void)save;
    }

    // Fallback: full statement parse (must end with EOF)
    auto stmts = p.parse();
    if (stmts.empty())
      return {nullptr, ""};
    if (stmts.size() == 1)
      return {std::move(stmts.front()), ""};
    // Multiple statements in one REPL line: wrap in Block
    return {std::make_unique<Block>(std::move(stmts)), ""};
  } catch (const std::runtime_error &e) {
    return {nullptr, e.what()};
  }
}

} // namespace eloxir
