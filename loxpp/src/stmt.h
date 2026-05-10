#pragma once

#include <memory>
#include <string>
#include <vector>

#include "expr.h"
#include "token.h"

namespace loxpp {

class BlockStmt;
class ClassStmt;
class ExpressionStmt;
class FunctionStmt;
class IfStmt;
class PrintStmt;
class ReturnStmt;
class VarStmt;
class WhileStmt;

class StmtVoidVisitor {
 public:
  virtual ~StmtVoidVisitor() = default;
  virtual void visitBlockStmt(BlockStmt& stmt) = 0;
  virtual void visitClassStmt(ClassStmt& stmt) = 0;
  virtual void visitExpressionStmt(ExpressionStmt& stmt) = 0;
  virtual void visitFunctionStmt(FunctionStmt& stmt) = 0;
  virtual void visitIfStmt(IfStmt& stmt) = 0;
  virtual void visitPrintStmt(PrintStmt& stmt) = 0;
  virtual void visitReturnStmt(ReturnStmt& stmt) = 0;
  virtual void visitVarStmt(VarStmt& stmt) = 0;
  virtual void visitWhileStmt(WhileStmt& stmt) = 0;
};

class StmtStringVisitor {
 public:
  virtual ~StmtStringVisitor() = default;
  virtual std::string visitBlockStmt(BlockStmt& stmt) = 0;
  virtual std::string visitClassStmt(ClassStmt& stmt) = 0;
  virtual std::string visitExpressionStmt(ExpressionStmt& stmt) = 0;
  virtual std::string visitFunctionStmt(FunctionStmt& stmt) = 0;
  virtual std::string visitIfStmt(IfStmt& stmt) = 0;
  virtual std::string visitPrintStmt(PrintStmt& stmt) = 0;
  virtual std::string visitReturnStmt(ReturnStmt& stmt) = 0;
  virtual std::string visitVarStmt(VarStmt& stmt) = 0;
  virtual std::string visitWhileStmt(WhileStmt& stmt) = 0;
};

class Stmt {
 public:
  virtual ~Stmt() = default;
  virtual void accept(StmtVoidVisitor& visitor) = 0;
  virtual std::string accept(StmtStringVisitor& visitor) = 0;
};

using StmtPtr = std::shared_ptr<Stmt>;
using FunctionStmtPtr = std::shared_ptr<FunctionStmt>;

class BlockStmt final : public Stmt {
 public:
  explicit BlockStmt(std::vector<StmtPtr> statements)
      : statements(std::move(statements)) {}

  void accept(StmtVoidVisitor& visitor) override;
  std::string accept(StmtStringVisitor& visitor) override;

  std::vector<StmtPtr> statements;
};

class ClassStmt final : public Stmt {
 public:
  ClassStmt(Token name,
            std::shared_ptr<VariableExpr> superclass,
            std::vector<FunctionStmtPtr> methods)
      : name(std::move(name)),
        superclass(std::move(superclass)),
        methods(std::move(methods)) {}

  void accept(StmtVoidVisitor& visitor) override;
  std::string accept(StmtStringVisitor& visitor) override;

  Token name;
  std::shared_ptr<VariableExpr> superclass;
  std::vector<FunctionStmtPtr> methods;
};

class ExpressionStmt final : public Stmt {
 public:
  explicit ExpressionStmt(ExprPtr expression)
      : expression(std::move(expression)) {}

  void accept(StmtVoidVisitor& visitor) override;
  std::string accept(StmtStringVisitor& visitor) override;

  ExprPtr expression;
};

class FunctionStmt final : public Stmt {
 public:
  FunctionStmt(Token name, std::vector<Token> params, std::vector<StmtPtr> body)
      : name(std::move(name)),
        params(std::move(params)),
        body(std::move(body)) {}

  void accept(StmtVoidVisitor& visitor) override;
  std::string accept(StmtStringVisitor& visitor) override;

  Token name;
  std::vector<Token> params;
  std::vector<StmtPtr> body;
};

class IfStmt final : public Stmt {
 public:
  IfStmt(ExprPtr condition, StmtPtr then_branch, StmtPtr else_branch)
      : condition(std::move(condition)),
        then_branch(std::move(then_branch)),
        else_branch(std::move(else_branch)) {}

  void accept(StmtVoidVisitor& visitor) override;
  std::string accept(StmtStringVisitor& visitor) override;

  ExprPtr condition;
  StmtPtr then_branch;
  StmtPtr else_branch;
};

class PrintStmt final : public Stmt {
 public:
  explicit PrintStmt(ExprPtr expression) : expression(std::move(expression)) {}

  void accept(StmtVoidVisitor& visitor) override;
  std::string accept(StmtStringVisitor& visitor) override;

  ExprPtr expression;
};

class ReturnStmt final : public Stmt {
 public:
  ReturnStmt(Token keyword, ExprPtr value)
      : keyword(std::move(keyword)), value(std::move(value)) {}

  void accept(StmtVoidVisitor& visitor) override;
  std::string accept(StmtStringVisitor& visitor) override;

  Token keyword;
  ExprPtr value;
};

class VarStmt final : public Stmt {
 public:
  VarStmt(Token name, ExprPtr initializer)
      : name(std::move(name)), initializer(std::move(initializer)) {}

  void accept(StmtVoidVisitor& visitor) override;
  std::string accept(StmtStringVisitor& visitor) override;

  Token name;
  ExprPtr initializer;
};

class WhileStmt final : public Stmt {
 public:
  WhileStmt(ExprPtr condition, StmtPtr body)
      : condition(std::move(condition)), body(std::move(body)) {}

  void accept(StmtVoidVisitor& visitor) override;
  std::string accept(StmtStringVisitor& visitor) override;

  ExprPtr condition;
  StmtPtr body;
};

}  // namespace loxpp
