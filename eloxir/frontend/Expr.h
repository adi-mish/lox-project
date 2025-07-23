#pragma once
#include "Token.h"
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace eloxir {

// Forward declaration of visitor interface
class ExprVisitor;

// Forward declarations
class Binary;
class Grouping;
class Literal;
class Unary;
class Variable;
class Assign;
class Logical;
class Call;
class Get;
class Set;
class This;
class Super;

class Expr {
public:
  virtual ~Expr() = default;
  virtual void accept(ExprVisitor *visitor) = 0;

  using Value = std::variant<std::monostate, double, std::string, bool>;
};

class Binary : public Expr {
public:
  std::unique_ptr<Expr> left;
  Token op;
  std::unique_ptr<Expr> right;
  Binary(std::unique_ptr<Expr> l, Token o, std::unique_ptr<Expr> r)
      : left(std::move(l)), op(std::move(o)), right(std::move(r)) {}
  void accept(ExprVisitor *v) override;
};

class Grouping : public Expr {
public:
  std::unique_ptr<Expr> expression;
  explicit Grouping(std::unique_ptr<Expr> e) : expression(std::move(e)) {}
  void accept(ExprVisitor *v) override;
};

class Literal : public Expr {
public:
  Value value;
  explicit Literal(Value v) : value(std::move(v)) {}
  void accept(ExprVisitor *v) override;
};

class Unary : public Expr {
public:
  Token op;
  std::unique_ptr<Expr> right;
  Unary(Token o, std::unique_ptr<Expr> r)
      : op(std::move(o)), right(std::move(r)) {}
  void accept(ExprVisitor *v) override;
};

class Variable : public Expr {
public:
  Token name;
  explicit Variable(Token n) : name(std::move(n)) {}
  void accept(ExprVisitor *v) override;
};

class Assign : public Expr {
public:
  Token name;
  std::unique_ptr<Expr> value;
  Assign(Token n, std::unique_ptr<Expr> v)
      : name(std::move(n)), value(std::move(v)) {}
  void accept(ExprVisitor *v) override;
};

class Logical : public Expr {
public:
  std::unique_ptr<Expr> left;
  Token op;
  std::unique_ptr<Expr> right;
  Logical(std::unique_ptr<Expr> l, Token o, std::unique_ptr<Expr> r)
      : left(std::move(l)), op(std::move(o)), right(std::move(r)) {}
  void accept(ExprVisitor *v) override;
};

class Call : public Expr {
public:
  std::unique_ptr<Expr> callee;
  Token paren;
  std::vector<std::unique_ptr<Expr>> arguments;
  Call(std::unique_ptr<Expr> c, Token p,
       std::vector<std::unique_ptr<Expr>> args)
      : callee(std::move(c)), paren(std::move(p)), arguments(std::move(args)) {}
  void accept(ExprVisitor *v) override;
};

class Get : public Expr {
public:
  std::unique_ptr<Expr> object;
  Token name;
  Get(std::unique_ptr<Expr> o, Token n)
      : object(std::move(o)), name(std::move(n)) {}
  void accept(ExprVisitor *v) override;
};

class Set : public Expr {
public:
  std::unique_ptr<Expr> object;
  Token name;
  std::unique_ptr<Expr> value;
  Set(std::unique_ptr<Expr> o, Token n, std::unique_ptr<Expr> v)
      : object(std::move(o)), name(std::move(n)), value(std::move(v)) {}
  void accept(ExprVisitor *v) override;
};

class This : public Expr {
public:
  Token keyword;
  explicit This(Token k) : keyword(std::move(k)) {}
  void accept(ExprVisitor *v) override;
};

class Super : public Expr {
public:
  Token keyword;
  Token method;
  Super(Token k, Token m) : keyword(std::move(k)), method(std::move(m)) {}
  void accept(ExprVisitor *v) override;
};

} // namespace eloxir
