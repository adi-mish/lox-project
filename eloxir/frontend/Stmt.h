#pragma once
#include "Expr.h"
#include <memory>
#include <vector>

namespace eloxir {

class CodeGenVisitor;

namespace Stmt {

class Expression;
class Print;
class Var;
class Block;
class If;
class While;
class Function;
class Return;

class Visitor {
public:
    virtual ~Visitor() = default;
    virtual void visitExpressionStmt(Expression* stmt) = 0;
    virtual void visitPrintStmt(Print* stmt) = 0;
    virtual void visitVarStmt(Var* stmt) = 0;
    virtual void visitBlockStmt(Block* stmt) = 0;
    virtual void visitIfStmt(If* stmt) = 0;
    virtual void visitWhileStmt(While* stmt) = 0;
    virtual void visitFunctionStmt(Function* stmt) = 0;
    virtual void visitReturnStmt(Return* stmt) = 0;
};

class Stmt {
public:
    virtual ~Stmt() = default;
    virtual void accept(Visitor* visitor) = 0;
    virtual void codegen(CodeGenVisitor& codeGen) = 0;
};

class Expression : public Stmt {
public:
    explicit Expression(std::unique_ptr<Expr::Expr> expression)
        : expression(std::move(expression)) {}

    void accept(Visitor* visitor) override {
        visitor->visitExpressionStmt(this);
    }
    
    void codegen(CodeGenVisitor& codeGen) override;

    std::unique_ptr<Expr::Expr> expression;
};

class Print : public Stmt {
public:
    explicit Print(std::unique_ptr<Expr::Expr> expression)
        : expression(std::move(expression)) {}

    void accept(Visitor* visitor) override {
        visitor->visitPrintStmt(this);
    }
    
    void codegen(CodeGenVisitor& codeGen) override;

    std::unique_ptr<Expr::Expr> expression;
};

class Var : public Stmt {
public:
    Var(Token name, std::unique_ptr<Expr::Expr> initializer)
        : name(std::move(name)), initializer(std::move(initializer)) {}

    void accept(Visitor* visitor) override {
        visitor->visitVarStmt(this);
    }
    
    void codegen(CodeGenVisitor& codeGen) override;

    Token name;
    std::unique_ptr<Expr::Expr> initializer;
};

class Block : public Stmt {
public:
    explicit Block(std::vector<std::unique_ptr<Stmt>> statements)
        : statements(std::move(statements)) {}

    void accept(Visitor* visitor) override {
        visitor->visitBlockStmt(this);
    }
    
    void codegen(CodeGenVisitor& codeGen) override;

    std::vector<std::unique_ptr<Stmt>> statements;
};

class If : public Stmt {
public:
    If(std::unique_ptr<Expr::Expr> condition, std::unique_ptr<Stmt> thenBranch, std::unique_ptr<Stmt> elseBranch)
        : condition(std::move(condition)), thenBranch(std::move(thenBranch)), elseBranch(std::move(elseBranch)) {}

    void accept(Visitor* visitor) override {
        visitor->visitIfStmt(this);
    }
    
    void codegen(CodeGenVisitor& codeGen) override;

    std::unique_ptr<Expr::Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
};

class While : public Stmt {
public:
    While(std::unique_ptr<Expr::Expr> condition, std::unique_ptr<Stmt> body)
        : condition(std::move(condition)), body(std::move(body)) {}

    void accept(Visitor* visitor) override {
        visitor->visitWhileStmt(this);
    }
    
    void codegen(CodeGenVisitor& codeGen) override;

    std::unique_ptr<Expr::Expr> condition;
    std::unique_ptr<Stmt> body;
};

class Function : public Stmt {
public:
    Function(Token name, std::vector<Token> params, std::vector<std::unique_ptr<Stmt>> body)
        : name(std::move(name)), params(std::move(params)), body(std::move(body)) {}

    void accept(Visitor* visitor) override {
        visitor->visitFunctionStmt(this);
    }
    
    void codegen(CodeGenVisitor& codeGen) override;

    Token name;
    std::vector<Token> params;
    std::vector<std::unique_ptr<Stmt>> body;
};

class Return : public Stmt {
public:
    Return(Token keyword, std::unique_ptr<Expr::Expr> value)
        : keyword(std::move(keyword)), value(std::move(value)) {}

    void accept(Visitor* visitor) override {
        visitor->visitReturnStmt(this);
    }
    
    void codegen(CodeGenVisitor& codeGen) override;

    Token keyword;
    std::unique_ptr<Expr::Expr> value;
};

} // namespace Stmt
} // namespace eloxir
