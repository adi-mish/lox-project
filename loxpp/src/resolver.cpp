#include "resolver.h"

namespace loxpp {

Resolver::Resolver(Interpreter& interpreter, ErrorReporter& reporter)
    : interpreter_(interpreter), reporter_(reporter) {}

void Resolver::resolve(const std::vector<StmtPtr>& statements) {
  for (const auto& statement : statements) {
    resolve(statement);
  }
}

void Resolver::visitBlockStmt(BlockStmt& stmt) {
  beginScope();
  resolve(stmt.statements);
  endScope();
}

void Resolver::visitClassStmt(ClassStmt& stmt) {
  const ClassType enclosing_class = current_class_;
  current_class_ = ClassType::Class;

  declare(stmt.name);
  define(stmt.name);

  if (stmt.superclass && stmt.name.lexeme == stmt.superclass->name.lexeme) {
    reporter_.error(stmt.superclass->name, "A class can't inherit from itself.");
  }

  if (stmt.superclass) {
    current_class_ = ClassType::Subclass;
    resolve(std::static_pointer_cast<Expr>(stmt.superclass));
  }

  if (stmt.superclass) {
    beginScope();
    scopes_.back()["super"] = true;
  }

  beginScope();
  scopes_.back()["this"] = true;

  for (const auto& method : stmt.methods) {
    FunctionType declaration = FunctionType::Method;
    if (method->name.lexeme == "init") {
      declaration = FunctionType::Initializer;
    }
    resolveFunction(*method, declaration);
  }

  endScope();

  if (stmt.superclass) {
    endScope();
  }

  current_class_ = enclosing_class;
}

void Resolver::visitExpressionStmt(ExpressionStmt& stmt) {
  resolve(stmt.expression);
}

void Resolver::visitFunctionStmt(FunctionStmt& stmt) {
  declare(stmt.name);
  define(stmt.name);
  resolveFunction(stmt, FunctionType::Function);
}

void Resolver::visitIfStmt(IfStmt& stmt) {
  resolve(stmt.condition);
  resolve(stmt.then_branch);
  if (stmt.else_branch) {
    resolve(stmt.else_branch);
  }
}

void Resolver::visitPrintStmt(PrintStmt& stmt) {
  resolve(stmt.expression);
}

void Resolver::visitReturnStmt(ReturnStmt& stmt) {
  if (current_function_ == FunctionType::None) {
    reporter_.error(stmt.keyword, "Can't return from top-level code.");
  }

  if (stmt.value) {
    if (current_function_ == FunctionType::Initializer) {
      reporter_.error(stmt.keyword,
                      "Can't return a value from an initializer.");
    }
    resolve(stmt.value);
  }
}

void Resolver::visitVarStmt(VarStmt& stmt) {
  declare(stmt.name);
  if (stmt.initializer) {
    resolve(stmt.initializer);
  }
  define(stmt.name);
}

void Resolver::visitWhileStmt(WhileStmt& stmt) {
  resolve(stmt.condition);
  resolve(stmt.body);
}

void Resolver::visitAssignExpr(AssignExpr& expr) {
  resolve(expr.value);
  resolveLocal(expr, expr.name);
}

void Resolver::visitBinaryExpr(BinaryExpr& expr) {
  resolve(expr.left);
  resolve(expr.right);
}

void Resolver::visitCallExpr(CallExpr& expr) {
  resolve(expr.callee);
  for (const auto& argument : expr.arguments) {
    resolve(argument);
  }
}

void Resolver::visitGetExpr(GetExpr& expr) {
  resolve(expr.object);
}

void Resolver::visitGroupingExpr(GroupingExpr& expr) {
  resolve(expr.expression);
}

void Resolver::visitLiteralExpr(LiteralExpr&) {}

void Resolver::visitLogicalExpr(LogicalExpr& expr) {
  resolve(expr.left);
  resolve(expr.right);
}

void Resolver::visitSetExpr(SetExpr& expr) {
  resolve(expr.value);
  resolve(expr.object);
}

void Resolver::visitSuperExpr(SuperExpr& expr) {
  if (current_class_ == ClassType::None) {
    reporter_.error(expr.keyword, "Can't use 'super' outside of a class.");
  } else if (current_class_ != ClassType::Subclass) {
    reporter_.error(expr.keyword,
                    "Can't use 'super' in a class with no superclass.");
  }

  resolveLocal(expr, expr.keyword);
}

void Resolver::visitThisExpr(ThisExpr& expr) {
  if (current_class_ == ClassType::None) {
    reporter_.error(expr.keyword, "Can't use 'this' outside of a class.");
    return;
  }

  resolveLocal(expr, expr.keyword);
}

void Resolver::visitUnaryExpr(UnaryExpr& expr) {
  resolve(expr.right);
}

void Resolver::visitVariableExpr(VariableExpr& expr) {
  if (!scopes_.empty()) {
    if (const auto it = scopes_.back().find(expr.name.lexeme);
        it != scopes_.back().end() && !it->second) {
      reporter_.error(expr.name,
                      "Can't read local variable in its own initializer.");
    }
  }

  resolveLocal(expr, expr.name);
}

void Resolver::resolve(const StmtPtr& stmt) {
  if (stmt) {
    stmt->accept(*this);
  }
}

void Resolver::resolve(const ExprPtr& expr) {
  if (expr) {
    expr->accept(*this);
  }
}

void Resolver::resolveFunction(FunctionStmt& function, FunctionType type) {
  const FunctionType enclosing_function = current_function_;
  current_function_ = type;

  beginScope();
  for (const auto& param : function.params) {
    declare(param);
    define(param);
  }
  resolve(function.body);
  endScope();

  current_function_ = enclosing_function;
}

void Resolver::beginScope() {
  scopes_.emplace_back();
}

void Resolver::endScope() {
  scopes_.pop_back();
}

void Resolver::declare(const Token& name) {
  if (scopes_.empty()) {
    return;
  }

  auto& scope = scopes_.back();
  if (scope.contains(name.lexeme)) {
    reporter_.error(name, "Already a variable with this name in this scope.");
  }
  scope[name.lexeme] = false;
}

void Resolver::define(const Token& name) {
  if (!scopes_.empty()) {
    scopes_.back()[name.lexeme] = true;
  }
}

void Resolver::resolveLocal(Expr& expr, const Token& name) {
  for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
    if (scopes_[static_cast<std::size_t>(i)].contains(name.lexeme)) {
      interpreter_.resolve(expr, static_cast<int>(scopes_.size()) - 1 - i);
      return;
    }
  }
}

}  // namespace loxpp
