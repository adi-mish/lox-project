#pragma once

#include <unordered_map>
#include <vector>

#include "error_reporter.h"
#include "interpreter.h"

namespace loxpp {

class Resolver final : public ExprVoidVisitor, public StmtVoidVisitor {
 public:
  Resolver(Interpreter& interpreter, ErrorReporter& reporter);

  void resolve(const std::vector<StmtPtr>& statements);

  void visitBlockStmt(BlockStmt& stmt) override;
  void visitClassStmt(ClassStmt& stmt) override;
  void visitExpressionStmt(ExpressionStmt& stmt) override;
  void visitFunctionStmt(FunctionStmt& stmt) override;
  void visitIfStmt(IfStmt& stmt) override;
  void visitPrintStmt(PrintStmt& stmt) override;
  void visitReturnStmt(ReturnStmt& stmt) override;
  void visitVarStmt(VarStmt& stmt) override;
  void visitWhileStmt(WhileStmt& stmt) override;

  void visitAssignExpr(AssignExpr& expr) override;
  void visitBinaryExpr(BinaryExpr& expr) override;
  void visitCallExpr(CallExpr& expr) override;
  void visitGetExpr(GetExpr& expr) override;
  void visitGroupingExpr(GroupingExpr& expr) override;
  void visitLiteralExpr(LiteralExpr& expr) override;
  void visitLogicalExpr(LogicalExpr& expr) override;
  void visitSetExpr(SetExpr& expr) override;
  void visitSuperExpr(SuperExpr& expr) override;
  void visitThisExpr(ThisExpr& expr) override;
  void visitUnaryExpr(UnaryExpr& expr) override;
  void visitVariableExpr(VariableExpr& expr) override;

 private:
  enum class FunctionType {
    None,
    Function,
    Initializer,
    Method,
  };

  enum class ClassType {
    None,
    Class,
    Subclass,
  };

  void resolve(const StmtPtr& stmt);
  void resolve(const ExprPtr& expr);
  void resolveFunction(FunctionStmt& function, FunctionType type);
  void beginScope();
  void endScope();
  void declare(const Token& name);
  void define(const Token& name);
  void resolveLocal(Expr& expr, const Token& name);

  Interpreter& interpreter_;
  ErrorReporter& reporter_;
  std::vector<std::unordered_map<std::string, bool>> scopes_;
  FunctionType current_function_ = FunctionType::None;
  ClassType current_class_ = ClassType::None;
};

}  // namespace loxpp
