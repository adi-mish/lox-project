#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "expr.h"
#include "stmt.h"

namespace loxpp {

class AstPrinter final : public ExprStringVisitor, public StmtStringVisitor {
 public:
  std::string print(const ExprPtr& expr);
  std::string print(const StmtPtr& stmt);

  std::string visitAssignExpr(AssignExpr& expr) override;
  std::string visitBinaryExpr(BinaryExpr& expr) override;
  std::string visitCallExpr(CallExpr& expr) override;
  std::string visitGetExpr(GetExpr& expr) override;
  std::string visitGroupingExpr(GroupingExpr& expr) override;
  std::string visitLiteralExpr(LiteralExpr& expr) override;
  std::string visitLogicalExpr(LogicalExpr& expr) override;
  std::string visitSetExpr(SetExpr& expr) override;
  std::string visitSuperExpr(SuperExpr& expr) override;
  std::string visitThisExpr(ThisExpr& expr) override;
  std::string visitUnaryExpr(UnaryExpr& expr) override;
  std::string visitVariableExpr(VariableExpr& expr) override;

  std::string visitBlockStmt(BlockStmt& stmt) override;
  std::string visitClassStmt(ClassStmt& stmt) override;
  std::string visitExpressionStmt(ExpressionStmt& stmt) override;
  std::string visitFunctionStmt(FunctionStmt& stmt) override;
  std::string visitIfStmt(IfStmt& stmt) override;
  std::string visitPrintStmt(PrintStmt& stmt) override;
  std::string visitReturnStmt(ReturnStmt& stmt) override;
  std::string visitVarStmt(VarStmt& stmt) override;
  std::string visitWhileStmt(WhileStmt& stmt) override;

 private:
  static std::string parenthesize(std::string_view name,
                                  const std::vector<std::string>& parts);
};

}  // namespace loxpp
