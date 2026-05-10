#include "lox_function.h"

#include <utility>

#include "interpreter.h"
#include "lox_instance.h"
#include "runtime_error.h"

namespace loxpp {

LoxFunction::LoxFunction(FunctionStmtPtr declaration,
                         std::shared_ptr<Environment> closure,
                         bool is_initializer)
    : declaration_(std::move(declaration)),
      closure_(std::move(closure)),
      is_initializer_(is_initializer) {}

std::shared_ptr<LoxFunction> LoxFunction::bind(
    std::shared_ptr<LoxInstance> instance) const {
  auto environment = std::make_shared<Environment>(closure_);
  environment->define("this", Value(std::move(instance)));
  return std::make_shared<LoxFunction>(declaration_, environment,
                                       is_initializer_);
}

int LoxFunction::arity() const {
  return static_cast<int>(declaration_->params.size());
}

Value LoxFunction::call(Interpreter& interpreter,
                        const std::vector<Value>& arguments) {
  auto environment = std::make_shared<Environment>(closure_);
  for (std::size_t i = 0; i < declaration_->params.size(); ++i) {
    environment->define(declaration_->params[i].lexeme, arguments[i]);
  }

  try {
    interpreter.executeBlock(declaration_->body, environment);
  } catch (const ReturnSignal& return_value) {
    if (is_initializer_) {
      return closure_->getAt(0, "this");
    }
    return return_value.value;
  }

  if (is_initializer_) {
    return closure_->getAt(0, "this");
  }
  return std::monostate{};
}

std::string LoxFunction::toString() const {
  return "<fn " + declaration_->name.lexeme + ">";
}

}  // namespace loxpp
