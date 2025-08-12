#pragma once
#include "../frontend/Expr.h"
#include "../frontend/Stmt.h"
#include "../frontend/Visitor.h"
#include "../runtime/Value.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <unordered_map>
#include <unordered_set>

namespace eloxir {

class CodeGenVisitor : public ExprVisitor, public StmtVisitor {
  llvm::IRBuilder<> builder;
  llvm::LLVMContext &ctx;
  llvm::Module &mod;
  // current lexical scope:
  std::unordered_map<std::string, llvm::Value *> locals;
  // track which locals are direct values (like parameters) vs alloca'd
  std::unordered_set<std::string> directValues;
  // function table for user-defined functions
  std::unordered_map<std::string, llvm::Function *> functions;
  // current function being compiled (for return statements)
  llvm::Function *currentFunction;
  bool builtinsInitialized;

public:
  CodeGenVisitor(llvm::Module &m);

  llvm::Type *llvmValueTy() const;
  llvm::Value *value; // last visited Expr result

  // Safe accessors for the builder / module
  llvm::IRBuilder<> &getBuilder() { return builder; }
  llvm::Module &getModule() { return mod; }
  llvm::LLVMContext &getContext() { return ctx; }

  // Access to globals for REPL persistence
  std::unordered_map<std::string, llvm::Value *> globals;

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
  llvm::Value *isNumber(llvm::Value *v);
  llvm::Value *isString(llvm::Value *v);
  llvm::Value *toDouble(llvm::Value *v);
  llvm::Value *fromDouble(llvm::Value *d);
  llvm::Value *boolConst(bool b);
  llvm::Value *nilConst();
  llvm::Value *makeBool(llvm::Value *i1);
  llvm::Value *stringConst(const std::string &str);
  llvm::Value *isFalsy(llvm::Value *v);  // returns i1
  llvm::Value *isTruthy(llvm::Value *v); // returns i1 (= !isFalsy)

  // New helper methods for proper comparisons
  llvm::Value *valuesEqual(llvm::Value *L, llvm::Value *R); // returns i1
  llvm::Value *checkBothNumbers(llvm::Value *L, llvm::Value *R,
                                llvm::BasicBlock *&successBB,
                                llvm::BasicBlock *&errorBB); // returns i1

  // Helper method to create built-in function objects
  void initializeBuiltins();
};

} // namespace eloxir
