#pragma once

namespace eloxir {

// Forward declarations for Expr node types
class Binary;
class Grouping;
class Literal;
class Unary;
class Variable;
class Assign;
class Logical;
class Call;
class Get;
class Set;
class This;
class Super;

// Forward declarations for Stmt node types
class Expression;
class Print;
class Var;
class Block;
class If;
class While;
class Function;
class Return;
class Class;

// ── Standalone ExprVisitor interface ──────────────────────────────────────
struct ExprVisitor {
  virtual ~ExprVisitor() = default;
  virtual void visitBinaryExpr(Binary *) = 0;
  virtual void visitGroupingExpr(Grouping *) = 0;
  virtual void visitLiteralExpr(Literal *) = 0;
  virtual void visitUnaryExpr(Unary *) = 0;
  virtual void visitVariableExpr(Variable *) = 0;
  virtual void visitAssignExpr(Assign *) = 0;
  virtual void visitLogicalExpr(Logical *) = 0;
  virtual void visitCallExpr(Call *) = 0;
  virtual void visitGetExpr(Get *) = 0;
  virtual void visitSetExpr(Set *) = 0;
  virtual void visitThisExpr(This *) = 0;
  virtual void visitSuperExpr(Super *) = 0;
};

// ── Standalone StmtVisitor interface ──────────────────────────────────────
struct StmtVisitor {
  virtual ~StmtVisitor() = default;
  virtual void visitExpressionStmt(Expression *) = 0;
  virtual void visitPrintStmt(Print *) = 0;
  virtual void visitVarStmt(Var *) = 0;
  virtual void visitBlockStmt(Block *) = 0;
  virtual void visitIfStmt(If *) = 0;
  virtual void visitWhileStmt(While *) = 0;
  virtual void visitFunctionStmt(Function *) = 0;
  virtual void visitReturnStmt(Return *) = 0;
  virtual void visitClassStmt(Class *) = 0;
};

} // namespace eloxir
