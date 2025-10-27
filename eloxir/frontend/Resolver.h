#pragma once
#include "Expr.h"
#include "Stmt.h"
#include "Token.h"
#include "Visitor.h"
#include <stack>
#include <string>
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

  static constexpr int MAX_LOCAL_SLOTS = 256;
  static constexpr int MAX_UPVALUES = 256;

  struct FunctionInfo {
    FunctionType type;
    std::vector<std::string> upvalues; // Names of captured variables
    std::unordered_map<std::string, int> upvalue_indices;
    std::string name;
    int localCount = 0;
  };

  std::vector<std::unordered_map<std::string, bool>> scopes;
  std::stack<FunctionInfo> function_stack;
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

  // Upvalue support
  void addUpvalue(const std::string &name);
  void addUpvalueChain(const std::string &name, int depth);
  int resolveUpvalue(Function *function, const Token &name);

public:
  // Access to upvalue information for code generation
  std::unordered_map<const Function *, std::vector<std::string>>
      function_upvalues;
};

} // namespace eloxir