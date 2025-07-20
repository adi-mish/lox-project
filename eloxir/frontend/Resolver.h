#pragma once
#include "Expr.h"
#include "Stmt.h"
#include <map>
#include <string>
#include <vector>

namespace eloxir {

class Resolver : public Expr::Visitor, public Stmt::Visitor {
public:
    Resolver();
    
    void resolve(const std::vector<std::unique_ptr<Stmt::Stmt>>& statements);
    void resolve(Stmt::Stmt* stmt);
    void resolve(Expr::Expr* expr);
    
    void visitBinaryExpr(Expr::Binary* expr) override;
    void visitGroupingExpr(Expr::Grouping* expr) override;
    void visitLiteralExpr(Expr::Literal* expr) override;
    void visitUnaryExpr(Expr::Unary* expr) override;
    void visitVariableExpr(Expr::Variable* expr) override;
    void visitAssignExpr(Expr::Assign* expr) override;
    void visitLogicalExpr(Expr::Logical* expr) override;
    void visitCallExpr(Expr::Call* expr) override;

    void visitExpressionStmt(Stmt::Expression* stmt) override;
    void visitPrintStmt(Stmt::Print* stmt) override;
    void visitVarStmt(Stmt::Var* stmt) override;
    void visitBlockStmt(Stmt::Block* stmt) override;
    void visitIfStmt(Stmt::If* stmt) override;
    void visitWhileStmt(Stmt::While* stmt) override;
    void visitFunctionStmt(Stmt::Function* stmt) override;
    void visitReturnStmt(Stmt::Return* stmt) override;

private:
    std::vector<std::map<std::string, bool>> scopes;
    
    enum class FunctionType {
        NONE,
        FUNCTION
    };
    
    FunctionType currentFunction = FunctionType::NONE;
    
    void beginScope();
    void endScope();
    void declare(const Token& name);
    void define(const Token& name);
    void resolveLocal(Expr::Expr* expr, const Token& name);
    void resolveFunction(Stmt::Function* function, FunctionType type);
};

} // namespace eloxir
