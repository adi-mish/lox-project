#include "Resolver.h"
#include <stdexcept>

namespace eloxir {

Resolver::Resolver() = default;

void Resolver::resolve(const std::vector<std::unique_ptr<Stmt>> &statements) {
  for (const auto &s : statements)
    resolve(s.get());
}

void Resolver::resolve(Stmt *stmt) { stmt->accept(this); }
void Resolver::resolve(Expr *expr) { expr->accept(this); }

void Resolver::beginScope() { scopes.emplace_back(); }
void Resolver::endScope() { scopes.pop_back(); }

void Resolver::declare(const Token &name) {
  if (scopes.empty())
    return;
  auto &scope = scopes.back();
  if (scope.find(name.getLexeme()) != scope.end()) {
    throw std::runtime_error("Variable already declared in this scope: " +
                             name.getLexeme());
  }
  scope[name.getLexeme()] = false;
}

void Resolver::define(const Token &name) {
  if (scopes.empty())
    return;
  scopes.back()[name.getLexeme()] = true;
}

void Resolver::resolveLocal(Expr *expr, const Token &name) {
  for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; --i) {
    if (scopes[i].count(name.getLexeme())) {
      locals[expr] = static_cast<int>(scopes.size()) - 1 - i;
      return;
    }
  }
  // global variable, leave unresolved
}

void Resolver::resolveFunction(Function *function, FunctionType type) {
  FunctionType enclosing = currentFunction;
  currentFunction = type;

  beginScope();
  for (const auto &param : function->params) {
    declare(param);
    define(param);
  }
  resolve(function->body.get());
  endScope();

  currentFunction = enclosing;
}

// === Stmt visitors ===
void Resolver::visitBlockStmt(Block *s) {
  beginScope();
  for (auto &st : s->statements)
    resolve(st.get());
  endScope();
}

void Resolver::visitVarStmt(Var *s) {
  declare(s->name);
  if (s->initializer)
    resolve(s->initializer.get());
  define(s->name);
}

void Resolver::visitFunctionStmt(Function *s) {
  declare(s->name);
  define(s->name);
  resolveFunction(s, FunctionType::FUNCTION);
}

void Resolver::visitExpressionStmt(Expression *s) {
  resolve(s->expression.get());
}

void Resolver::visitIfStmt(If *s) {
  resolve(s->condition.get());
  resolve(s->thenBranch.get());
  if (s->elseBranch)
    resolve(s->elseBranch.get());
}

void Resolver::visitPrintStmt(Print *s) { resolve(s->expression.get()); }

void Resolver::visitReturnStmt(Return *s) {
  if (currentFunction == FunctionType::NONE) {
    throw std::runtime_error("Can't return from top-level code.");
  }
  if (s->value) {
    if (currentFunction == FunctionType::INITIALIZER) {
      throw std::runtime_error("Can't return a value from an initializer.");
    }
    resolve(s->value.get());
  }
}

void Resolver::visitWhileStmt(While *s) {
  resolve(s->condition.get());
  resolve(s->body.get());
}

void Resolver::visitClassStmt(Class *s) {
  ClassType enclosingClass = currentClass;
  currentClass = ClassType::CLASS;

  declare(s->name);
  define(s->name);

  if (s->superclass && s->superclass->name.getLexeme() == s->name.getLexeme()) {
    throw std::runtime_error("A class can't inherit from itself.");
  }

  if (s->superclass) {
    currentClass = ClassType::SUBCLASS;
    resolve(s->superclass.get());

    beginScope();
    scopes.back()["super"] = true;
  }

  beginScope();
  scopes.back()["this"] = true;

  for (auto &method : s->methods) {
    FunctionType decl = (method->name.getLexeme() == "init")
                            ? FunctionType::INITIALIZER
                            : FunctionType::METHOD;
    resolveFunction(method.get(), decl);
  }
  endScope();

  if (s->superclass)
    endScope();
  currentClass = enclosingClass;
}

// === Expr visitors ===
void Resolver::visitAssignExpr(Assign *e) {
  resolve(e->value.get());
  resolveLocal(e, e->name);
}

void Resolver::visitBinaryExpr(Binary *e) {
  resolve(e->left.get());
  resolve(e->right.get());
}

void Resolver::visitCallExpr(Call *e) {
  resolve(e->callee.get());
  for (auto &arg : e->arguments)
    resolve(arg.get());
}

void Resolver::visitGroupingExpr(Grouping *e) { resolve(e->expression.get()); }

void Resolver::visitLiteralExpr(Literal *) { /* nothing */
}

void Resolver::visitLogicalExpr(Logical *e) {
  resolve(e->left.get());
  resolve(e->right.get());
}

void Resolver::visitUnaryExpr(Unary *e) { resolve(e->right.get()); }

void Resolver::visitVariableExpr(Variable *e) {
  if (!scopes.empty()) {
    auto &scope = scopes.back();
    const auto &lex = e->name.getLexeme();
    if (scope.count(lex) && scope[lex] == false) {
      throw std::runtime_error(
          "Can't read local variable in its own initializer.");
    }
  }
  resolveLocal(e, e->name);
}

void Resolver::visitGetExpr(Get *e) { resolve(e->object.get()); }

void Resolver::visitSetExpr(Set *e) {
  resolve(e->value.get());
  resolve(e->object.get());
}

void Resolver::visitThisExpr(This *e) {
  if (currentClass == ClassType::NONE) {
    throw std::runtime_error("Can't use 'this' outside of a class.");
  }
  resolveLocal(e, e->keyword);
}

void Resolver::visitSuperExpr(Super *e) {
  if (currentClass == ClassType::NONE) {
    throw std::runtime_error("Can't use 'super' outside of a class.");
  } else if (currentClass != ClassType::SUBCLASS) {
    throw std::runtime_error(
        "Can't use 'super' in a class with no superclass.");
  }
  resolveLocal(e, e->keyword);
}

} // namespace eloxir