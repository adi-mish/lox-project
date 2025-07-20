#pragma once
#include "Token.h"
#include <memory>
#include <vector>

namespace eloxir {

// Forward declarations
class CodeGenVisitor;

namespace Expr {

class Binary;
class Grouping;
class Literal;
class Unary;
class Variable;
class Assign;
class Logical;
class Call;

class Visitor {
public:
    virtual ~Visitor() = default;
    virtual void visitBinaryExpr(Binary* expr) = 0;
    virtual void visitGroupingExpr(Grouping* expr) = 0;
    virtual void visitLiteralExpr(Literal* expr) = 0;
    virtual void visitUnaryExpr(Unary* expr) = 0;
    virtual void visitVariableExpr(Variable* expr) = 0;
    virtual void visitAssignExpr(Assign* expr) = 0;
    virtual void visitLogicalExpr(Logical* expr) = 0;
    virtual void visitCallExpr(Call* expr) = 0;
};

class Expr {
public:
    virtual ~Expr() = default;
    virtual void accept(Visitor* visitor) = 0;
};

class Binary : public Expr {
public:
    Binary(std::unique_ptr<Expr> left, Token op, std::unique_ptr<Expr> right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

    void accept(Visitor* visitor) override {
        visitor->visitBinaryExpr(this);
    }

    std::unique_ptr<Expr> left;
    Token op;
    std::unique_ptr<Expr> right;
};

class Grouping : public Expr {
public:
    explicit Grouping(std::unique_ptr<Expr> expression)
        : expression(std::move(expression)) {}

    void accept(Visitor* visitor) override {
        visitor->visitGroupingExpr(this);
    }

    std::unique_ptr<Expr> expression;
};

class Literal : public Expr {
public:
    explicit Literal(std::variant<std::monostate, double, std::string, bool> value)
        : value(std::move(value)) {}

    void accept(Visitor* visitor) override {
        visitor->visitLiteralExpr(this);
    }

    std::variant<std::monostate, double, std::string, bool> value;
};

class Unary : public Expr {
public:
    Unary(Token op, std::unique_ptr<Expr> right)
        : op(std::move(op)), right(std::move(right)) {}

    void accept(Visitor* visitor) override {
        visitor->visitUnaryExpr(this);
    }

    Token op;
    std::unique_ptr<Expr> right;
};

class Variable : public Expr {
public:
    explicit Variable(Token name)
        : name(std::move(name)) {}

    void accept(Visitor* visitor) override {
        visitor->visitVariableExpr(this);
    }

    Token name;
};

class Assign : public Expr {
public:
    Assign(Token name, std::unique_ptr<Expr> value)
        : name(std::move(name)), value(std::move(value)) {}

    void accept(Visitor* visitor) override {
        visitor->visitAssignExpr(this);
    }

    Token name;
    std::unique_ptr<Expr> value;
};

class Logical : public Expr {
public:
    Logical(std::unique_ptr<Expr> left, Token op, std::unique_ptr<Expr> right)
        : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

    void accept(Visitor* visitor) override {
        visitor->visitLogicalExpr(this);
    }

    std::unique_ptr<Expr> left;
    Token op;
    std::unique_ptr<Expr> right;
};

class Call : public Expr {
public:
    Call(std::unique_ptr<Expr> callee, Token paren, std::vector<std::unique_ptr<Expr>> arguments)
        : callee(std::move(callee)), paren(std::move(paren)), arguments(std::move(arguments)) {}

    void accept(Visitor* visitor) override {
        visitor->visitCallExpr(this);
    }

    std::unique_ptr<Expr> callee;
    Token paren;
    std::vector<std::unique_ptr<Expr>> arguments;
};

} // namespace Expr
} // namespace eloxir
