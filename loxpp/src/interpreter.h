#pragma once

#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include "environment.h"
#include "error_reporter.h"
#include "expr.h"
#include "stmt.h"

namespace loxpp {

class Interpreter final : public ExprValueVisitor, public StmtVoidVisitor {
 public:
  explicit Interpreter(ErrorReporter& reporter);

  void interpret(const std::vector<StmtPtr>& statements);
  void resolve(const Expr& expr, int depth);
  void executeBlock(const std::vector<StmtPtr>& statements,
                    std::shared_ptr<Environment> environment);

  Value visitAssignExpr(AssignExpr& expr) override;
  Value visitBinaryExpr(BinaryExpr& expr) override;
  Value visitCallExpr(CallExpr& expr) override;
  Value visitGetExpr(GetExpr& expr) override;
  Value visitGroupingExpr(GroupingExpr& expr) override;
  Value visitLiteralExpr(LiteralExpr& expr) override;
  Value visitLogicalExpr(LogicalExpr& expr) override;
  Value visitSetExpr(SetExpr& expr) override;
  Value visitSuperExpr(SuperExpr& expr) override;
  Value visitThisExpr(ThisExpr& expr) override;
  Value visitUnaryExpr(UnaryExpr& expr) override;
  Value visitVariableExpr(VariableExpr& expr) override;

  void visitBlockStmt(BlockStmt& stmt) override;
  void visitClassStmt(ClassStmt& stmt) override;
  void visitExpressionStmt(ExpressionStmt& stmt) override;
  void visitFunctionStmt(FunctionStmt& stmt) override;
  void visitIfStmt(IfStmt& stmt) override;
  void visitPrintStmt(PrintStmt& stmt) override;
  void visitReturnStmt(ReturnStmt& stmt) override;
  void visitVarStmt(VarStmt& stmt) override;
  void visitWhileStmt(WhileStmt& stmt) override;

 private:
  [[nodiscard]] Value lookUpVariable(const Token& name, const Expr& expr) const;
  static void checkNumberOperand(const Token& op, const Value& operand);
  static void checkNumberOperands(const Token& op,
                                  const Value& left,
                                  const Value& right);

  Value evaluate(const ExprPtr& expr);
  void execute(const StmtPtr& stmt);

  ErrorReporter& reporter_;
  std::shared_ptr<Environment> globals_;
  std::shared_ptr<Environment> environment_;
  std::unordered_map<const Expr*, int> locals_;
};

}  // namespace loxpp
