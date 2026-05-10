#include "CodeGenPlan.h"
#include "../frontend/Expr.h"
#include "../frontend/Stmt.h"

#include <unordered_set>

namespace eloxir {

namespace {

struct ClassCandidate {
  PlannedClass planned;
  bool eligible = false;
};

bool isTrivialInitializer(const Function &method) {
  if (!method.body) {
    return true;
  }

  const auto &statements = method.body->statements;
  if (statements.empty()) {
    return true;
  }

  if (statements.size() == 1) {
    if (auto *ret = dynamic_cast<Return *>(statements.front().get())) {
      return ret->value == nullptr;
    }
  }

  return false;
}

void visitExpr(const Expr *expr, std::unordered_set<std::string> &assigned);

void visitStmt(const Stmt *stmt, std::unordered_set<std::string> &assigned) {
  if (!stmt) {
    return;
  }

  if (auto *expression = dynamic_cast<const Expression *>(stmt)) {
    visitExpr(expression->expression.get(), assigned);
  } else if (auto *print = dynamic_cast<const Print *>(stmt)) {
    visitExpr(print->expression.get(), assigned);
  } else if (auto *var = dynamic_cast<const Var *>(stmt)) {
    visitExpr(var->initializer.get(), assigned);
  } else if (auto *block = dynamic_cast<const Block *>(stmt)) {
    for (const auto &child : block->statements) {
      visitStmt(child.get(), assigned);
    }
  } else if (auto *ifStmt = dynamic_cast<const If *>(stmt)) {
    visitExpr(ifStmt->condition.get(), assigned);
    visitStmt(ifStmt->thenBranch.get(), assigned);
    visitStmt(ifStmt->elseBranch.get(), assigned);
  } else if (auto *whileStmt = dynamic_cast<const While *>(stmt)) {
    visitExpr(whileStmt->condition.get(), assigned);
    visitStmt(whileStmt->body.get(), assigned);
  } else if (auto *function = dynamic_cast<const Function *>(stmt)) {
    if (function->body) {
      visitStmt(function->body.get(), assigned);
    }
  } else if (auto *returnStmt = dynamic_cast<const Return *>(stmt)) {
    visitExpr(returnStmt->value.get(), assigned);
  } else if (auto *klass = dynamic_cast<const Class *>(stmt)) {
    visitExpr(klass->superclass.get(), assigned);
    for (const auto &method : klass->methods) {
      visitStmt(method.get(), assigned);
    }
  }
}

void visitExpr(const Expr *expr, std::unordered_set<std::string> &assigned) {
  if (!expr) {
    return;
  }

  if (auto *binary = dynamic_cast<const Binary *>(expr)) {
    visitExpr(binary->left.get(), assigned);
    visitExpr(binary->right.get(), assigned);
  } else if (auto *grouping = dynamic_cast<const Grouping *>(expr)) {
    visitExpr(grouping->expression.get(), assigned);
  } else if (auto *unary = dynamic_cast<const Unary *>(expr)) {
    visitExpr(unary->right.get(), assigned);
  } else if (auto *assign = dynamic_cast<const Assign *>(expr)) {
    assigned.insert(assign->name.getLexeme());
    visitExpr(assign->value.get(), assigned);
  } else if (auto *logical = dynamic_cast<const Logical *>(expr)) {
    visitExpr(logical->left.get(), assigned);
    visitExpr(logical->right.get(), assigned);
  } else if (auto *call = dynamic_cast<const Call *>(expr)) {
    visitExpr(call->callee.get(), assigned);
    for (const auto &arg : call->arguments) {
      visitExpr(arg.get(), assigned);
    }
  } else if (auto *get = dynamic_cast<const Get *>(expr)) {
    visitExpr(get->object.get(), assigned);
  } else if (auto *set = dynamic_cast<const Set *>(expr)) {
    visitExpr(set->object.get(), assigned);
    visitExpr(set->value.get(), assigned);
  }
}

} // namespace

CodeGenPlan CodeGenPlan::analyze(
    const std::vector<std::unique_ptr<Stmt>> &statements) {
  CodeGenPlan plan;
  std::unordered_map<std::string, ClassCandidate> candidates;
  std::unordered_set<std::string> assigned;

  for (const auto &stmt : statements) {
    if (auto *klass = dynamic_cast<Class *>(stmt.get())) {
      ClassCandidate candidate;
      candidate.eligible = true;
      candidate.planned.initializerArity = 0;
      candidate.planned.trivialInitializer = klass->superclass == nullptr;

      for (const auto &method : klass->methods) {
        if (method->name.getLexeme() != "init") {
          continue;
        }

        candidate.planned.initializerArity =
            static_cast<int>(method->params.size());
        candidate.planned.trivialInitializer = isTrivialInitializer(*method);
        break;
      }

      candidates[klass->name.getLexeme()] = candidate;
    }

    visitStmt(stmt.get(), assigned);
  }

  for (auto &[name, candidate] : candidates) {
    if (candidate.eligible && candidate.planned.trivialInitializer &&
        assigned.find(name) == assigned.end()) {
      plan.stableClasses.emplace(name, candidate.planned);
    }
  }

  return plan;
}

const PlannedClass *
CodeGenPlan::findStableTrivialClass(const std::string &name) const {
  auto it = stableClasses.find(name);
  return it == stableClasses.end() ? nullptr : &it->second;
}

} // namespace eloxir
