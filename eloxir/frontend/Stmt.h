#pragma once
#include "Expr.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eloxir {
class CodeGenVisitor;
class StmtVisitor;
} // namespace eloxir

namespace eloxir {

// Forward declarations
class Expression;
class Print;
class Var;
class Block;
class If;
class While;
class Function;
class Return;
class Class;

class Stmt {
public:
  virtual ~Stmt() = default;
  virtual void accept(StmtVisitor *visitor) = 0;
  virtual void codegen(eloxir::CodeGenVisitor &) {
  } // default no-op until backend is ready
};

class Expression : public Stmt {
public:
  std::unique_ptr<Expr> expression;
  explicit Expression(std::unique_ptr<Expr> e) : expression(std::move(e)) {}
  void accept(StmtVisitor *v) override;
  void codegen(eloxir::CodeGenVisitor &cg) override;
};

class Print : public Stmt {
public:
  std::unique_ptr<Expr> expression;
  explicit Print(std::unique_ptr<Expr> e) : expression(std::move(e)) {}
  void accept(StmtVisitor *v) override;
  void codegen(eloxir::CodeGenVisitor &cg) override;
};

class Var : public Stmt {
public:
  Token name;
  std::unique_ptr<Expr> initializer; // may be null
  Var(Token n, std::unique_ptr<Expr> init)
      : name(std::move(n)), initializer(std::move(init)) {}
  void accept(StmtVisitor *v) override;
  void codegen(eloxir::CodeGenVisitor &cg) override;
};

class Block : public Stmt {
public:
  std::vector<std::unique_ptr<Stmt>> statements;
  explicit Block(std::vector<std::unique_ptr<Stmt>> s)
      : statements(std::move(s)) {}
  void accept(StmtVisitor *v) override;
  void codegen(eloxir::CodeGenVisitor &cg) override;
};

class If : public Stmt {
public:
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> thenBranch;
  std::unique_ptr<Stmt> elseBranch;
  If(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> thenB,
     std::unique_ptr<Stmt> elseB)
      : condition(std::move(cond)), thenBranch(std::move(thenB)),
        elseBranch(std::move(elseB)) {}
  void accept(StmtVisitor *v) override;
  void codegen(eloxir::CodeGenVisitor &cg) override;
};

class While : public Stmt {
public:
  std::unique_ptr<Expr> condition;
  std::unique_ptr<Stmt> body;
  While(std::unique_ptr<Expr> cond, std::unique_ptr<Stmt> b)
      : condition(std::move(cond)), body(std::move(b)) {}
  void accept(StmtVisitor *v) override;
  void codegen(eloxir::CodeGenVisitor &cg) override;
};

class Function : public Stmt {
public:
  Token name;
  std::vector<Token> params;
  std::unique_ptr<Block> body;
  Function(Token n, std::vector<Token> p, std::unique_ptr<Block> b)
      : name(std::move(n)), params(std::move(p)), body(std::move(b)) {}
  void accept(StmtVisitor *v) override;
  void codegen(eloxir::CodeGenVisitor &cg) override;
};

class Return : public Stmt {
public:
  Token keyword;
  std::unique_ptr<Expr> value; // may be null
  Return(Token k, std::unique_ptr<Expr> v)
      : keyword(std::move(k)), value(std::move(v)) {}
  void accept(StmtVisitor *v) override;
  void codegen(eloxir::CodeGenVisitor &cg) override;
};

class Class : public Stmt {
public:
  Token name;
  std::unique_ptr<Variable> superclass; // nullable
  std::vector<std::unique_ptr<Function>> methods;
  Class(Token n, std::unique_ptr<Variable> sc,
        std::vector<std::unique_ptr<Function>> m)
      : name(std::move(n)), superclass(std::move(sc)), methods(std::move(m)) {}
  void accept(StmtVisitor *v) override;
  void codegen(eloxir::CodeGenVisitor &cg) override; // TODO
};

} // namespace eloxir
