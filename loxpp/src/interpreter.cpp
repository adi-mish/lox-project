#include "interpreter.h"

#include <chrono>
#include <memory>
#include <utility>

#include "callable.h"
#include "lox_class.h"
#include "lox_function.h"
#include "lox_instance.h"
#include "runtime_error.h"

namespace loxpp {

namespace {

class NativeClock final : public LoxCallable {
 public:
  [[nodiscard]] int arity() const override { return 0; }

  Value call(Interpreter&, const std::vector<Value>&) override {
    using Clock = std::chrono::system_clock;
    const auto now = Clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
  }

  [[nodiscard]] std::string toString() const override { return "<native fn>"; }
};

std::shared_ptr<LoxCallable> asCallable(const Value& value) {
  if (const auto* callable = std::get_if<std::shared_ptr<LoxCallable>>(&value)) {
    return *callable;
  }
  return nullptr;
}

std::shared_ptr<LoxClass> asClass(const Value& value) {
  return std::dynamic_pointer_cast<LoxClass>(asCallable(value));
}

std::shared_ptr<LoxInstance> asInstance(const Value& value) {
  if (const auto* instance = std::get_if<std::shared_ptr<LoxInstance>>(&value)) {
    return *instance;
  }
  return nullptr;
}

double asNumber(const Value& value) {
  return std::get<double>(value);
}

}  // namespace

Interpreter::Interpreter(ErrorReporter& reporter)
    : reporter_(reporter),
      globals_(std::make_shared<Environment>()),
      environment_(globals_) {
  globals_->define("clock", Value(std::make_shared<NativeClock>()));
}

void Interpreter::interpret(const std::vector<StmtPtr>& statements) {
  try {
    for (const auto& statement : statements) {
      execute(statement);
    }
  } catch (const RuntimeError& error) {
    reporter_.runtimeError(error);
  }
}

void Interpreter::resolve(const Expr& expr, int depth) {
  locals_[&expr] = depth;
}

void Interpreter::executeBlock(const std::vector<StmtPtr>& statements,
                               std::shared_ptr<Environment> environment) {
  const auto previous = environment_;
  environment_ = std::move(environment);

  try {
    for (const auto& statement : statements) {
      execute(statement);
    }
  } catch (...) {
    environment_ = previous;
    throw;
  }

  environment_ = previous;
}

Value Interpreter::visitAssignExpr(AssignExpr& expr) {
  Value value = evaluate(expr.value);
  if (const auto it = locals_.find(&expr); it != locals_.end()) {
    environment_->assignAt(it->second, expr.name, value);
  } else {
    globals_->assign(expr.name, value);
  }
  return value;
}

Value Interpreter::visitBinaryExpr(BinaryExpr& expr) {
  const Value left = evaluate(expr.left);
  const Value right = evaluate(expr.right);

  switch (expr.op.type) {
    case TokenType::BangEqual:
      return !isEqual(left, right);
    case TokenType::EqualEqual:
      return isEqual(left, right);
    case TokenType::Greater:
      checkNumberOperands(expr.op, left, right);
      return asNumber(left) > asNumber(right);
    case TokenType::GreaterEqual:
      checkNumberOperands(expr.op, left, right);
      return asNumber(left) >= asNumber(right);
    case TokenType::Less:
      checkNumberOperands(expr.op, left, right);
      return asNumber(left) < asNumber(right);
    case TokenType::LessEqual:
      checkNumberOperands(expr.op, left, right);
      return asNumber(left) <= asNumber(right);
    case TokenType::Minus:
      checkNumberOperands(expr.op, left, right);
      return asNumber(left) - asNumber(right);
    case TokenType::Plus:
      if (std::holds_alternative<double>(left) &&
          std::holds_alternative<double>(right)) {
        return asNumber(left) + asNumber(right);
      }
      if (std::holds_alternative<std::string>(left) &&
          std::holds_alternative<std::string>(right)) {
        return std::get<std::string>(left) + std::get<std::string>(right);
      }
      throw RuntimeError(expr.op,
                         "Operands must be two numbers or two strings.");
    case TokenType::Slash:
      checkNumberOperands(expr.op, left, right);
      return asNumber(left) / asNumber(right);
    case TokenType::Star:
      checkNumberOperands(expr.op, left, right);
      return asNumber(left) * asNumber(right);
    default:
      break;
  }

  return std::monostate{};
}

Value Interpreter::visitCallExpr(CallExpr& expr) {
  Value callee = evaluate(expr.callee);

  std::vector<Value> arguments;
  arguments.reserve(expr.arguments.size());
  for (const auto& argument : expr.arguments) {
    arguments.push_back(evaluate(argument));
  }

  auto function = asCallable(callee);
  if (!function) {
    throw RuntimeError(expr.paren, "Can only call functions and classes.");
  }

  if (static_cast<int>(arguments.size()) != function->arity()) {
    throw RuntimeError(expr.paren,
                       "Expected " + std::to_string(function->arity()) +
                           " arguments but got " +
                           std::to_string(arguments.size()) + ".");
  }

  return function->call(*this, arguments);
}

Value Interpreter::visitGetExpr(GetExpr& expr) {
  Value object = evaluate(expr.object);
  if (auto instance = asInstance(object)) {
    return instance->get(expr.name);
  }

  throw RuntimeError(expr.name, "Only instances have properties.");
}

Value Interpreter::visitGroupingExpr(GroupingExpr& expr) {
  return evaluate(expr.expression);
}

Value Interpreter::visitLiteralExpr(LiteralExpr& expr) {
  return expr.value;
}

Value Interpreter::visitLogicalExpr(LogicalExpr& expr) {
  Value left = evaluate(expr.left);

  if (expr.op.type == TokenType::Or) {
    if (isTruthy(left)) {
      return left;
    }
  } else if (!isTruthy(left)) {
    return left;
  }

  return evaluate(expr.right);
}

Value Interpreter::visitSetExpr(SetExpr& expr) {
  Value object = evaluate(expr.object);

  auto instance = asInstance(object);
  if (!instance) {
    throw RuntimeError(expr.name, "Only instances have fields.");
  }

  Value value = evaluate(expr.value);
  instance->set(expr.name, value);
  return value;
}

Value Interpreter::visitSuperExpr(SuperExpr& expr) {
  const int distance = locals_.at(&expr);
  auto superclass = asClass(environment_->getAt(distance, "super"));
  auto object = asInstance(environment_->getAt(distance - 1, "this"));

  auto method = superclass->findMethod(expr.method.lexeme);
  if (!method) {
    throw RuntimeError(expr.method,
                       "Undefined property '" + expr.method.lexeme + "'.");
  }

  return Value(std::static_pointer_cast<LoxCallable>(method->bind(object)));
}

Value Interpreter::visitThisExpr(ThisExpr& expr) {
  return lookUpVariable(expr.keyword, expr);
}

Value Interpreter::visitUnaryExpr(UnaryExpr& expr) {
  Value right = evaluate(expr.right);

  switch (expr.op.type) {
    case TokenType::Bang:
      return !isTruthy(right);
    case TokenType::Minus:
      checkNumberOperand(expr.op, right);
      return -asNumber(right);
    default:
      break;
  }

  return std::monostate{};
}

Value Interpreter::visitVariableExpr(VariableExpr& expr) {
  return lookUpVariable(expr.name, expr);
}

void Interpreter::visitBlockStmt(BlockStmt& stmt) {
  executeBlock(stmt.statements, std::make_shared<Environment>(environment_));
}

void Interpreter::visitClassStmt(ClassStmt& stmt) {
  std::shared_ptr<LoxClass> superclass;
  if (stmt.superclass) {
    Value superclass_value = evaluate(stmt.superclass);
    superclass = asClass(superclass_value);
    if (!superclass) {
      throw RuntimeError(stmt.superclass->name, "Superclass must be a class.");
    }
  }

  environment_->define(stmt.name.lexeme, std::monostate{});

  if (stmt.superclass) {
    environment_ = std::make_shared<Environment>(environment_);
    environment_->define(
        "super", Value(std::static_pointer_cast<LoxCallable>(superclass)));
  }

  std::unordered_map<std::string, std::shared_ptr<LoxFunction>> methods;
  for (const auto& method : stmt.methods) {
    auto function = std::make_shared<LoxFunction>(
        method, environment_, method->name.lexeme == "init");
    methods[method->name.lexeme] = function;
  }

  auto klass = std::make_shared<LoxClass>(stmt.name.lexeme, superclass, methods);

  if (superclass) {
    environment_ = environment_->enclosing();
  }

  environment_->assign(stmt.name,
                       Value(std::static_pointer_cast<LoxCallable>(klass)));
}

void Interpreter::visitExpressionStmt(ExpressionStmt& stmt) {
  evaluate(stmt.expression);
}

void Interpreter::visitFunctionStmt(FunctionStmt& stmt) {
  auto declaration =
      std::static_pointer_cast<FunctionStmt>(stmt.shared_from_this());
  auto function = std::make_shared<LoxFunction>(declaration, environment_, false);
  environment_->define(stmt.name.lexeme,
                       Value(std::static_pointer_cast<LoxCallable>(function)));
}

void Interpreter::visitIfStmt(IfStmt& stmt) {
  if (isTruthy(evaluate(stmt.condition))) {
    execute(stmt.then_branch);
  } else if (stmt.else_branch) {
    execute(stmt.else_branch);
  }
}

void Interpreter::visitPrintStmt(PrintStmt& stmt) {
  std::cout << stringifyValue(evaluate(stmt.expression)) << '\n';
}

void Interpreter::visitReturnStmt(ReturnStmt& stmt) {
  Value value = std::monostate{};
  if (stmt.value) {
    value = evaluate(stmt.value);
  }
  throw ReturnSignal(value);
}

void Interpreter::visitVarStmt(VarStmt& stmt) {
  Value value = std::monostate{};
  if (stmt.initializer) {
    value = evaluate(stmt.initializer);
  }

  environment_->define(stmt.name.lexeme, value);
}

void Interpreter::visitWhileStmt(WhileStmt& stmt) {
  while (isTruthy(evaluate(stmt.condition))) {
    execute(stmt.body);
  }
}

Value Interpreter::lookUpVariable(const Token& name, const Expr& expr) const {
  if (const auto it = locals_.find(&expr); it != locals_.end()) {
    return environment_->getAt(it->second, name.lexeme);
  }
  return globals_->get(name);
}

void Interpreter::checkNumberOperand(const Token& op, const Value& operand) {
  if (std::holds_alternative<double>(operand)) {
    return;
  }
  throw RuntimeError(op, "Operand must be a number.");
}

void Interpreter::checkNumberOperands(const Token& op,
                                      const Value& left,
                                      const Value& right) {
  if (std::holds_alternative<double>(left) &&
      std::holds_alternative<double>(right)) {
    return;
  }
  throw RuntimeError(op, "Operands must be numbers.");
}

Value Interpreter::evaluate(const ExprPtr& expr) {
  return expr->accept(*this);
}

void Interpreter::execute(const StmtPtr& stmt) {
  if (stmt) {
    stmt->accept(*this);
  }
}

}  // namespace loxpp
