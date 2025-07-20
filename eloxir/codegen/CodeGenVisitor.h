#pragma once
#include "frontend/Expr.h"
#include "frontend/Stmt.h"
#include "runtime/Value.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <unordered_map>

namespace eloxir {

class CodeGenVisitor : public Expr::Visitor, public Stmt::Visitor {
    llvm::IRBuilder<>  builder;
    llvm::LLVMContext& ctx;
    llvm::Module&      mod;
    // current lexical scope:
    std::unordered_map<std::string, llvm::Value*> locals;

public:
    CodeGenVisitor(llvm::Module& m);
    
    llvm::Type* llvmValueTy() const;
    llvm::Value* value; // last visited Expr result
    
    // == Expr nodes ==================================================
    void visitBinaryExpr(Expr::Binary* e) override;
    void visitGroupingExpr(Expr::Grouping* e) override;
    void visitLiteralExpr(Expr::Literal* e) override;
    void visitUnaryExpr(Expr::Unary* e) override;
    void visitVariableExpr(Expr::Variable* e) override;
    void visitAssignExpr(Expr::Assign* e) override;
    void visitLogicalExpr(Expr::Logical* e) override;
    void visitCallExpr(Expr::Call* e) override;
    
    // == Stmt nodes ==================================================
    void visitExpressionStmt(Stmt::Expression* s) override;
    void visitPrintStmt(Stmt::Print* s) override;
    void visitVarStmt(Stmt::Var* s) override;
    void visitBlockStmt(Stmt::Block* s) override;
    void visitIfStmt(Stmt::If* s) override;
    void visitWhileStmt(Stmt::While* s) override;
    void visitFunctionStmt(Stmt::Function* s) override;
    void visitReturnStmt(Stmt::Return* s) override;

private:
    llvm::Value* tagOf(llvm::Value* v);
};

} // namespace eloxir
