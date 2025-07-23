#pragma once
#include "Expr.h"
#include "Stmt.h"
#include "Token.h"
#include "Visitor.h"
#include <stack>
#include <unordered_map>
#include <vector>

namespace eloxir {

class Resolver : public ExprVisitor, public StmtVisitor {
public:
  Resolver();
  void resolve(const std::vector<std::unique_ptr<Stmt>> &statements);

  // depth map for backend/interpreter
  std::unordered_map<const Expr *, int> locals;

  // == Stmt
  void visitBlockStmt(Block *) override;
  void visitVarStmt(Var *) override;
  void visitFunctionStmt(Function *) override;
  void visitExpressionStmt(Expression *) override;
  void visitIfStmt(If *) override;
  void visitPrintStmt(Print *) override;
  void visitReturnStmt(Return *) override;
  void visitWhileStmt(While *) override;
  void visitClassStmt(Class *) override;

  // == Expr
  void visitAssignExpr(Assign *) override;
  void visitBinaryExpr(Binary *) override;
  void visitCallExpr(Call *) override;
  void visitGroupingExpr(Grouping *) override;
  void visitLiteralExpr(Literal *) override;
  void visitLogicalExpr(Logical *) override;
  void visitUnaryExpr(Unary *) override;
  void visitVariableExpr(Variable *) override;
  void visitGetExpr(Get *) override;
  void visitSetExpr(Set *) override;
  void visitThisExpr(This *) override;
  void visitSuperExpr(Super *) override;

private:
  enum class FunctionType { NONE, FUNCTION, INITIALIZER, METHOD };
  enum class ClassType { NONE, CLASS, SUBCLASS };

  std::vector<std::unordered_map<std::string, bool>> scopes;
  FunctionType currentFunction = FunctionType::NONE;
  ClassType currentClass = ClassType::NONE;

  void beginScope();
  void endScope();
  void declare(const Token &name);
  void define(const Token &name);
  void resolve(Stmt *stmt);
  void resolve(Expr *expr);
  void resolveFunction(Function *function, FunctionType type);
  void resolveLocal(Expr *expr, const Token &name);
};

} // namespace eloxir