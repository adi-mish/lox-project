#include "CodeGenPlan.h"
#include "../frontend/Expr.h"
#include "../frontend/Stmt.h"

#include <cstdint>
#include <unordered_set>

namespace eloxir {

namespace {

struct ClassCandidate {
  PlannedClass planned;
  bool eligible = false;
};

struct ProgramFacts {
  std::unordered_set<std::string> assignedVariables;
  std::unordered_set<std::string> writtenProperties;
  std::unordered_map<std::string, int> topLevelVarDeclarations;
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

bool isThisExpr(const Expr *expr) { return dynamic_cast<const This *>(expr); }

bool collectInitializerFields(const Function &method,
                              std::unordered_map<std::string, uint32_t> &slots) {
  if (!method.body) {
    return true;
  }

  for (const auto &stmt : method.body->statements) {
    const auto *expression = dynamic_cast<const Expression *>(stmt.get());
    if (!expression) {
      if (const auto *ret = dynamic_cast<const Return *>(stmt.get())) {
        return ret->value == nullptr;
      }
      return false;
    }

    const auto *set = dynamic_cast<const Set *>(expression->expression.get());
    if (!set || !isThisExpr(set->object.get())) {
      return false;
    }

    const std::string &fieldName = set->name.getLexeme();
    if (slots.find(fieldName) == slots.end()) {
      slots.emplace(fieldName, static_cast<uint32_t>(slots.size()));
    }
  }

  return true;
}

const Get *asThisGetter(const Expr *expr) {
  const auto *get = dynamic_cast<const Get *>(expr);
  if (!get || !isThisExpr(get->object.get())) {
    return nullptr;
  }
  return get;
}

bool classifyMethod(const Function &method,
                    const std::unordered_map<std::string, uint32_t> &fieldSlots,
                    PlannedMethod &planned) {
  if (method.name.getLexeme() == "init" || !method.body) {
    return false;
  }

  planned.arity = static_cast<int>(method.params.size());
  const auto &statements = method.body->statements;
  if (statements.empty()) {
    planned.kind = PlannedMethodKind::Empty;
    return true;
  }

  if (statements.size() != 1) {
    return false;
  }

  const auto *ret = dynamic_cast<const Return *>(statements.front().get());
  if (!ret) {
    return false;
  }

  if (!ret->value) {
    planned.kind = PlannedMethodKind::Empty;
    return true;
  }

  const auto *get = asThisGetter(ret->value.get());
  if (!get || !method.params.empty()) {
    return false;
  }

  const std::string &fieldName = get->name.getLexeme();
  auto slotIt = fieldSlots.find(fieldName);
  if (slotIt == fieldSlots.end()) {
    return false;
  }

  planned.kind = PlannedMethodKind::FieldGetter;
  planned.fieldName = fieldName;
  planned.fieldSlot = slotIt->second;
  return true;
}

void visitExpr(const Expr *expr, ProgramFacts &facts);

void visitStmt(const Stmt *stmt, ProgramFacts &facts) {
  if (!stmt) {
    return;
  }

  if (auto *expression = dynamic_cast<const Expression *>(stmt)) {
    visitExpr(expression->expression.get(), facts);
  } else if (auto *print = dynamic_cast<const Print *>(stmt)) {
    visitExpr(print->expression.get(), facts);
  } else if (auto *var = dynamic_cast<const Var *>(stmt)) {
    visitExpr(var->initializer.get(), facts);
  } else if (auto *block = dynamic_cast<const Block *>(stmt)) {
    for (const auto &child : block->statements) {
      visitStmt(child.get(), facts);
    }
  } else if (auto *ifStmt = dynamic_cast<const If *>(stmt)) {
    visitExpr(ifStmt->condition.get(), facts);
    visitStmt(ifStmt->thenBranch.get(), facts);
    visitStmt(ifStmt->elseBranch.get(), facts);
  } else if (auto *whileStmt = dynamic_cast<const While *>(stmt)) {
    visitExpr(whileStmt->condition.get(), facts);
    visitStmt(whileStmt->body.get(), facts);
  } else if (auto *function = dynamic_cast<const Function *>(stmt)) {
    if (function->body) {
      visitStmt(function->body.get(), facts);
    }
  } else if (auto *returnStmt = dynamic_cast<const Return *>(stmt)) {
    visitExpr(returnStmt->value.get(), facts);
  } else if (auto *klass = dynamic_cast<const Class *>(stmt)) {
    visitExpr(klass->superclass.get(), facts);
    for (const auto &method : klass->methods) {
      visitStmt(method.get(), facts);
    }
  }
}

void visitExpr(const Expr *expr, ProgramFacts &facts) {
  if (!expr) {
    return;
  }

  if (auto *binary = dynamic_cast<const Binary *>(expr)) {
    visitExpr(binary->left.get(), facts);
    visitExpr(binary->right.get(), facts);
  } else if (auto *grouping = dynamic_cast<const Grouping *>(expr)) {
    visitExpr(grouping->expression.get(), facts);
  } else if (auto *unary = dynamic_cast<const Unary *>(expr)) {
    visitExpr(unary->right.get(), facts);
  } else if (auto *assign = dynamic_cast<const Assign *>(expr)) {
    facts.assignedVariables.insert(assign->name.getLexeme());
    visitExpr(assign->value.get(), facts);
  } else if (auto *logical = dynamic_cast<const Logical *>(expr)) {
    visitExpr(logical->left.get(), facts);
    visitExpr(logical->right.get(), facts);
  } else if (auto *call = dynamic_cast<const Call *>(expr)) {
    visitExpr(call->callee.get(), facts);
    for (const auto &arg : call->arguments) {
      visitExpr(arg.get(), facts);
    }
  } else if (auto *get = dynamic_cast<const Get *>(expr)) {
    visitExpr(get->object.get(), facts);
  } else if (auto *set = dynamic_cast<const Set *>(expr)) {
    facts.writtenProperties.insert(set->name.getLexeme());
    visitExpr(set->object.get(), facts);
    visitExpr(set->value.get(), facts);
  }
}

const Call *asClassInitializerCall(const Expr *expr, std::string &className) {
  const auto *call = dynamic_cast<const Call *>(expr);
  if (!call) {
    return nullptr;
  }

  const auto *callee = dynamic_cast<const Variable *>(call->callee.get());
  if (!callee) {
    return nullptr;
  }

  className = callee->name.getLexeme();
  return call;
}

struct EscapeFacts {
  std::unordered_set<std::string> escapingReceivers;
  std::unordered_set<std::string> escapingClasses;
  std::unordered_set<std::string> dynamicMethodNames;
};

bool isOptimizedReceiverCall(const Call *call, const CodeGenPlan &plan) {
  if (!call || !call->arguments.empty()) {
    return false;
  }

  const auto *get = dynamic_cast<const Get *>(call->callee.get());
  if (!get) {
    return false;
  }

  const auto *receiver = dynamic_cast<const Variable *>(get->object.get());
  if (!receiver) {
    return false;
  }

  const PlannedMethod *method = plan.findStableReceiverMethod(
      receiver->name.getLexeme(), get->name.getLexeme());
  return method && method->arity == 0;
}

void visitExprEscapes(const Expr *expr, const CodeGenPlan &plan,
                      EscapeFacts &facts);

void visitStmtEscapes(const Stmt *stmt, const CodeGenPlan &plan,
                      EscapeFacts &facts) {
  if (!stmt) {
    return;
  }

  if (const auto *var = dynamic_cast<const Var *>(stmt)) {
    const PlannedReceiver *receiver =
        plan.findStableReceiver(var->name.getLexeme());
    std::string className;
    const Call *initCall =
        asClassInitializerCall(var->initializer.get(), className);
    if (receiver && initCall && receiver->className == className) {
      for (const auto &arg : initCall->arguments) {
        visitExprEscapes(arg.get(), plan, facts);
      }
      return;
    }
    visitExprEscapes(var->initializer.get(), plan, facts);
  } else if (const auto *expression = dynamic_cast<const Expression *>(stmt)) {
    visitExprEscapes(expression->expression.get(), plan, facts);
  } else if (const auto *print = dynamic_cast<const Print *>(stmt)) {
    visitExprEscapes(print->expression.get(), plan, facts);
  } else if (const auto *block = dynamic_cast<const Block *>(stmt)) {
    for (const auto &child : block->statements) {
      visitStmtEscapes(child.get(), plan, facts);
    }
  } else if (const auto *ifStmt = dynamic_cast<const If *>(stmt)) {
    visitExprEscapes(ifStmt->condition.get(), plan, facts);
    visitStmtEscapes(ifStmt->thenBranch.get(), plan, facts);
    visitStmtEscapes(ifStmt->elseBranch.get(), plan, facts);
  } else if (const auto *whileStmt = dynamic_cast<const While *>(stmt)) {
    visitExprEscapes(whileStmt->condition.get(), plan, facts);
    visitStmtEscapes(whileStmt->body.get(), plan, facts);
  } else if (const auto *function = dynamic_cast<const Function *>(stmt)) {
    if (function->body) {
      visitStmtEscapes(function->body.get(), plan, facts);
    }
  } else if (const auto *returnStmt = dynamic_cast<const Return *>(stmt)) {
    visitExprEscapes(returnStmt->value.get(), plan, facts);
  } else if (const auto *klass = dynamic_cast<const Class *>(stmt)) {
    visitExprEscapes(klass->superclass.get(), plan, facts);
    for (const auto &method : klass->methods) {
      visitStmtEscapes(method.get(), plan, facts);
    }
  }
}

void visitExprEscapes(const Expr *expr, const CodeGenPlan &plan,
                      EscapeFacts &facts) {
  if (!expr) {
    return;
  }

  if (const auto *call = dynamic_cast<const Call *>(expr)) {
    if (isOptimizedReceiverCall(call, plan)) {
      for (const auto &arg : call->arguments) {
        visitExprEscapes(arg.get(), plan, facts);
      }
      return;
    }

    visitExprEscapes(call->callee.get(), plan, facts);
    for (const auto &arg : call->arguments) {
      visitExprEscapes(arg.get(), plan, facts);
    }
  } else if (const auto *get = dynamic_cast<const Get *>(expr)) {
    if (const auto *receiver =
            dynamic_cast<const Variable *>(get->object.get())) {
      if (plan.findStableReceiver(receiver->name.getLexeme())) {
        facts.escapingReceivers.insert(receiver->name.getLexeme());
      }
    }
    facts.dynamicMethodNames.insert(get->name.getLexeme());
    visitExprEscapes(get->object.get(), plan, facts);
  } else if (const auto *set = dynamic_cast<const Set *>(expr)) {
    facts.dynamicMethodNames.insert(set->name.getLexeme());
    visitExprEscapes(set->object.get(), plan, facts);
    visitExprEscapes(set->value.get(), plan, facts);
  } else if (const auto *variable = dynamic_cast<const Variable *>(expr)) {
    const std::string &name = variable->name.getLexeme();
    if (plan.findStableReceiver(name)) {
      facts.escapingReceivers.insert(name);
    }
    if (plan.findStableClass(name)) {
      facts.escapingClasses.insert(name);
    }
  } else if (const auto *binary = dynamic_cast<const Binary *>(expr)) {
    visitExprEscapes(binary->left.get(), plan, facts);
    visitExprEscapes(binary->right.get(), plan, facts);
  } else if (const auto *grouping = dynamic_cast<const Grouping *>(expr)) {
    visitExprEscapes(grouping->expression.get(), plan, facts);
  } else if (const auto *unary = dynamic_cast<const Unary *>(expr)) {
    visitExprEscapes(unary->right.get(), plan, facts);
  } else if (const auto *assign = dynamic_cast<const Assign *>(expr)) {
    visitExprEscapes(assign->value.get(), plan, facts);
  } else if (const auto *logical = dynamic_cast<const Logical *>(expr)) {
    visitExprEscapes(logical->left.get(), plan, facts);
    visitExprEscapes(logical->right.get(), plan, facts);
  }
}

} // namespace

CodeGenPlan CodeGenPlan::analyze(
    const std::vector<std::unique_ptr<Stmt>> &statements) {
  CodeGenPlan plan;
  std::unordered_map<std::string, ClassCandidate> candidates;
  ProgramFacts facts;

  for (const auto &stmt : statements) {
    if (auto *var = dynamic_cast<Var *>(stmt.get())) {
      ++facts.topLevelVarDeclarations[var->name.getLexeme()];
    }
    visitStmt(stmt.get(), facts);
  }

  for (const auto &stmt : statements) {
    if (auto *klass = dynamic_cast<Class *>(stmt.get())) {
      ClassCandidate candidate;
      candidate.eligible = true;
      candidate.planned.initializerArity = 0;
      candidate.planned.hasSuperclass = klass->superclass != nullptr;
      candidate.planned.trivialInitializer = !candidate.planned.hasSuperclass;
      std::unordered_map<std::string, uint32_t> initializerSlots;
      bool initializerSlotsStable = !candidate.planned.hasSuperclass;

      for (const auto &method : klass->methods) {
        if (method->name.getLexeme() != "init") {
          continue;
        }

        candidate.planned.initializerArity =
            static_cast<int>(method->params.size());
        candidate.planned.trivialInitializer = isTrivialInitializer(*method);
        initializerSlotsStable =
            !candidate.planned.hasSuperclass &&
            collectInitializerFields(*method, initializerSlots);
        break;
      }
      candidate.planned.linearInitializer = initializerSlotsStable;

      if (!candidate.planned.hasSuperclass) {
        for (const auto &method : klass->methods) {
          const std::string &methodName = method->name.getLexeme();
          if (facts.writtenProperties.find(methodName) !=
              facts.writtenProperties.end()) {
            continue;
          }

          PlannedMethod plannedMethod;
          const auto &slots = initializerSlotsStable
                                  ? initializerSlots
                                  : std::unordered_map<std::string, uint32_t>{};
          if (classifyMethod(*method, slots, plannedMethod)) {
            candidate.planned.methods.emplace(methodName,
                                             std::move(plannedMethod));
          }
        }
      }

      candidates[klass->name.getLexeme()] = candidate;
    }
  }

  for (auto &[name, candidate] : candidates) {
    if (candidate.eligible &&
        facts.assignedVariables.find(name) == facts.assignedVariables.end()) {
      plan.stableClasses.emplace(name, candidate.planned);
    }
  }

  for (const auto &stmt : statements) {
    auto *var = dynamic_cast<Var *>(stmt.get());
    if (!var) {
      continue;
    }

    const std::string &varName = var->name.getLexeme();
    if (facts.topLevelVarDeclarations[varName] != 1 ||
        facts.assignedVariables.find(varName) != facts.assignedVariables.end()) {
      continue;
    }

    std::string className;
    const Call *call = asClassInitializerCall(var->initializer.get(), className);
    if (!call) {
      continue;
    }

    auto classIt = plan.stableClasses.find(className);
    if (classIt == plan.stableClasses.end()) {
      continue;
    }

    if (classIt->second.initializerArity !=
        static_cast<int>(call->arguments.size())) {
      continue;
    }

    plan.stableReceivers.emplace(varName, PlannedReceiver{className});
  }

  EscapeFacts escapes;
  for (const auto &stmt : statements) {
    visitStmtEscapes(stmt.get(), plan, escapes);
  }

  std::unordered_map<std::string, int> receiverCountByClass;
  std::unordered_map<std::string, int> escapingReceiverCountByClass;
  for (const auto &[receiverName, receiver] : plan.stableReceivers) {
    ++receiverCountByClass[receiver.className];
    if (escapes.escapingReceivers.find(receiverName) !=
        escapes.escapingReceivers.end()) {
      ++escapingReceiverCountByClass[receiver.className];
    }
  }

  for (auto &[className, klass] : plan.stableClasses) {
    if (klass.hasSuperclass || !klass.linearInitializer ||
        receiverCountByClass[className] == 0 ||
        escapingReceiverCountByClass[className] != 0 ||
        escapes.escapingClasses.find(className) != escapes.escapingClasses.end()) {
      continue;
    }

    for (const auto &[methodName, method] : klass.methods) {
      if (method.arity == 0 &&
          escapes.dynamicMethodNames.find(methodName) ==
              escapes.dynamicMethodNames.end()) {
        klass.omittableMethods.insert(methodName);
      }
    }
  }

  return plan;
}

const PlannedClass *
CodeGenPlan::findStableClass(const std::string &name) const {
  auto it = stableClasses.find(name);
  return it == stableClasses.end() ? nullptr : &it->second;
}

const PlannedClass *
CodeGenPlan::findStableTrivialClass(const std::string &name) const {
  auto it = stableClasses.find(name);
  if (it == stableClasses.end() || !it->second.trivialInitializer) {
    return nullptr;
  }
  return &it->second;
}

const PlannedReceiver *
CodeGenPlan::findStableReceiver(const std::string &name) const {
  auto it = stableReceivers.find(name);
  return it == stableReceivers.end() ? nullptr : &it->second;
}

const PlannedMethod *CodeGenPlan::findStableReceiverMethod(
    const std::string &receiverName, const std::string &methodName) const {
  const PlannedReceiver *receiver = findStableReceiver(receiverName);
  if (!receiver) {
    return nullptr;
  }

  const PlannedClass *klass = findStableClass(receiver->className);
  if (!klass || klass->hasSuperclass) {
    return nullptr;
  }

  auto methodIt = klass->methods.find(methodName);
  return methodIt == klass->methods.end() ? nullptr : &methodIt->second;
}

bool CodeGenPlan::canOmitMethodObject(const std::string &className,
                                      const std::string &methodName) const {
  const PlannedClass *klass = findStableClass(className);
  if (!klass) {
    return false;
  }
  return klass->omittableMethods.find(methodName) !=
         klass->omittableMethods.end();
}

} // namespace eloxir
