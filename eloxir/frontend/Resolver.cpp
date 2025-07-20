#include "Resolver.h"
#include <stdexcept>

namespace eloxir {

Resolver::Resolver() {}

void Resolver::resolve(const std::vector<std::unique_ptr<Stmt::Stmt>>& statements) {
    for (const auto& statement : statements) {
        resolve(statement.get());
    }
}

void Resolver::resolve(Stmt::Stmt* stmt) {
    stmt->accept(this);
}

void Resolver::resolve(Expr::Expr* expr) {
    expr->accept(this);
}

void Resolver::visitBinaryExpr(Expr::Binary* expr) {
    resolve(expr->left.get());
    resolve(expr->right.get());
}

void Resolver::visitGroupingExpr(Expr::Grouping* expr) {
    resolve(expr->expression.get());
}

void Resolver::visitLiteralExpr(Expr::Literal* /*expr*/) {
    // Nothing to resolve
}

void Resolver::visitUnaryExpr(Expr::Unary* expr) {
    resolve(expr->right.get());
}

void Resolver::visitVariableExpr(Expr::Variable* expr) {
    if (!scopes.empty()) {
        auto& scope = scopes.back();
        auto it = scope.find(expr->name.getLexeme());
        if (it != scope.end() && it->second == false) {
            throw std::runtime_error("Can't read local variable in its own initializer.");
        }
    }
    
    resolveLocal(expr, expr->name);
}

void Resolver::visitAssignExpr(Expr::Assign* expr) {
    resolve(expr->value.get());
    resolveLocal(expr, expr->name);
}

void Resolver::visitLogicalExpr(Expr::Logical* expr) {
    resolve(expr->left.get());
    resolve(expr->right.get());
}

void Resolver::visitCallExpr(Expr::Call* expr) {
    resolve(expr->callee.get());
    
    for (const auto& argument : expr->arguments) {
        resolve(argument.get());
    }
}

void Resolver::visitExpressionStmt(Stmt::Expression* stmt) {
    resolve(stmt->expression.get());
}

void Resolver::visitPrintStmt(Stmt::Print* stmt) {
    resolve(stmt->expression.get());
}

void Resolver::visitVarStmt(Stmt::Var* stmt) {
    declare(stmt->name);
    if (stmt->initializer != nullptr) {
        resolve(stmt->initializer.get());
    }
    define(stmt->name);
}

void Resolver::visitBlockStmt(Stmt::Block* stmt) {
    beginScope();
    resolve(stmt->statements);
    endScope();
}

void Resolver::visitIfStmt(Stmt::If* stmt) {
    resolve(stmt->condition.get());
    resolve(stmt->thenBranch.get());
    if (stmt->elseBranch != nullptr) {
        resolve(stmt->elseBranch.get());
    }
}

void Resolver::visitWhileStmt(Stmt::While* stmt) {
    resolve(stmt->condition.get());
    resolve(stmt->body.get());
}

void Resolver::visitFunctionStmt(Stmt::Function* stmt) {
    declare(stmt->name);
    define(stmt->name);
    
    resolveFunction(stmt, FunctionType::FUNCTION);
}

void Resolver::visitReturnStmt(Stmt::Return* stmt) {
    if (currentFunction == FunctionType::NONE) {
        throw std::runtime_error("Can't return from top-level code.");
    }
    
    if (stmt->value != nullptr) {
        resolve(stmt->value.get());
    }
}

void Resolver::beginScope() {
    scopes.emplace_back();
}

void Resolver::endScope() {
    scopes.pop_back();
}

void Resolver::declare(const Token& name) {
    if (scopes.empty()) return;
    
    auto& scope = scopes.back();
    if (scope.find(name.getLexeme()) != scope.end()) {
        throw std::runtime_error("Already a variable with this name in this scope.");
    }
    scope[name.getLexeme()] = false;
}

void Resolver::define(const Token& name) {
    if (scopes.empty()) return;
    scopes.back()[name.getLexeme()] = true;
}

void Resolver::resolveLocal(Expr::Expr* expr, const Token& name) {
    for (int i = scopes.size() - 1; i >= 0; i--) {
        if (scopes[i].find(name.getLexeme()) != scopes[i].end()) {
            // In real implementation, this would set the depth to i
            return;
        }
    }
}

void Resolver::resolveFunction(Stmt::Function* function, FunctionType type) {
    FunctionType enclosingFunction = currentFunction;
    currentFunction = type;
    
    beginScope();
    for (const auto& param : function->params) {
        declare(param);
        define(param);
    }
    resolve(function->body);
    endScope();
    
    currentFunction = enclosingFunction;
}

} // namespace eloxir
