#include "Resolver.h"
#include "CompileError.h"
#include <iostream>
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

  if (!function_stack.empty()) {
    FunctionInfo &info = function_stack.top();
    if (info.localCount >= MAX_LOCAL_SLOTS - 1) {
      throw CompileError("Too many local variables in function.");
    }
    info.localCount++;
  }
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

      // Check if this is an upvalue (accessed from enclosing scope)
      if (i < static_cast<int>(scopes.size()) - 1 && !function_stack.empty()) {
        // Variable found in enclosing scope - this is an upvalue!
        // We need to ensure this variable is captured by ALL intermediate
        // functions
        addUpvalueChain(name.getLexeme(),
                        static_cast<int>(scopes.size()) - 1 - i);
      }
      return;
    }
  }
  // global variable, leave unresolved
}

void Resolver::addUpvalue(const std::string &name) {
  if (function_stack.empty()) {
    return; // Not in a function
  }

  FunctionInfo &current_func = function_stack.top();

  // Check if upvalue already exists
  if (current_func.upvalue_indices.find(name) !=
      current_func.upvalue_indices.end()) {
    return; // Already captured
  }

  // Add new upvalue
  if (current_func.upvalues.size() >= MAX_UPVALUES) {
    throw CompileError("Too many closure variables in function.");
  }
  int index = static_cast<int>(current_func.upvalues.size());
  current_func.upvalues.push_back(name);
  current_func.upvalue_indices[name] = index;
}

void Resolver::addUpvalueChain(const std::string &name, int depth) {
  // Add upvalue to all intermediate function levels
  // depth = 0 means current scope, depth = 1 means immediate parent, etc.

  if (function_stack.empty()) {
    return; // No functions to add upvalues to
  }

  // Add to current function
  addUpvalue(name);

  // For multi-level upvalue capture, we need to ensure that when functions
  // complete, their upvalues are propagated to parent functions.
  // This will be handled in the function completion logic.
}

int Resolver::resolveUpvalue(Function *function, const Token &name) {
  if (function_stack.empty()) {
    return -1; // Not an upvalue
  }

  FunctionInfo &current_func = function_stack.top();
  auto it = current_func.upvalue_indices.find(name.getLexeme());
  if (it != current_func.upvalue_indices.end()) {
    return it->second;
  }

  return -1; // Not an upvalue
}

void Resolver::resolveFunction(Function *function, FunctionType type) {
  FunctionType enclosing = currentFunction;
  currentFunction = type;

  // Push new function context
  FunctionInfo func_info;
  func_info.type = type;
  func_info.name = function->name.getLexeme();
  func_info.localCount = 0;
  function_stack.push(func_info);

  beginScope();

  for (const auto &param : function->params) {
    declare(param);
    define(param);
  }
  if (function->body) {
    for (const auto &stmt : function->body->statements) {
      resolve(stmt.get());
    }
  }
  endScope();

  // Pop function context and store upvalue info
  FunctionInfo completed_func = function_stack.top();
  function_stack.pop();
  function_upvalues[function] = completed_func.upvalues;

  // CRITICAL: Propagate upvalues to parent function
  // If this function has upvalues, the parent function must also capture them
  // to enable multi-level closure capture
  if (!function_stack.empty() && !completed_func.upvalues.empty()) {
    FunctionInfo &parent_func = function_stack.top();
    for (const std::string &upvalue_name : completed_func.upvalues) {
      // Only propagate upvalues that the parent function cannot provide locally
      // Check if this variable exists in the parent function's LOCAL scope only
      // (not in its parent scopes)
      bool found_in_parent_local_scope = false;

      // The last scope in the stack belongs to the parent function
      if (!scopes.empty()) {
        found_in_parent_local_scope = scopes.back().count(upvalue_name) > 0;
      }

      // Only propagate if the parent function cannot provide this variable from
      // its own locals
      if (!found_in_parent_local_scope) {
        // Add this upvalue to the parent function if it doesn't already have it
        if (parent_func.upvalue_indices.find(upvalue_name) ==
            parent_func.upvalue_indices.end()) {
          if (parent_func.upvalues.size() >= MAX_UPVALUES) {
            throw CompileError("Too many closure variables in function.");
          }
          parent_func.upvalue_indices[upvalue_name] =
              static_cast<int>(parent_func.upvalues.size());
          parent_func.upvalues.push_back(upvalue_name);
        }
      }
    }
  }

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