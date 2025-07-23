#pragma once
#include "frontend/Expr.h"
#include "frontend/Stmt.h"
#include "frontend/Visitor.h"
#include "runtime/Value.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <unordered_map>

namespace eloxir {

class CodeGenVisitor : public ExprVisitor, public StmtVisitor {
  llvm::IRBuilder<> builder;
  llvm::LLVMContext &ctx;
  llvm::Module &mod;
  // current lexical scope:
  std::unordered_map<std::string, llvm::Value *> locals;

public:
  CodeGenVisitor(llvm::Module &m);

  llvm::Type *llvmValueTy() const;
  llvm::Value *value; // last visited Expr result

  // == Expr nodes ==================================================
  void visitBinaryExpr(Binary *e) override;
  void visitGroupingExpr(Grouping *e) override;
  void visitLiteralExpr(Literal *e) override;
  void visitUnaryExpr(Unary *e) override;
  void visitVariableExpr(Variable *e) override;
  void visitAssignExpr(Assign *e) override;
  void visitLogicalExpr(Logical *e) override;
  void visitCallExpr(Call *e) override;
  void visitGetExpr(Get *e) override;
  void visitSetExpr(Set *e) override;
  void visitThisExpr(This *e) override;
  void visitSuperExpr(Super *e) override;

  // == Stmt nodes ==================================================
  void visitExpressionStmt(Expression *s) override;
  void visitPrintStmt(Print *s) override;
  void visitVarStmt(Var *s) override;
  void visitBlockStmt(Block *s) override;
  void visitIfStmt(If *s) override;
  void visitWhileStmt(While *s) override;
  void visitFunctionStmt(Function *s) override;
  void visitReturnStmt(Return *s) override;
  void visitClassStmt(Class *s) override;

private:
  llvm::Value *tagOf(llvm::Value *v);
};

} // namespace eloxir
