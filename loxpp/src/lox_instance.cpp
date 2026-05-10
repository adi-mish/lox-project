#include "lox_instance.h"

#include <utility>

#include "lox_class.h"
#include "lox_function.h"
#include "runtime_error.h"

namespace loxpp {

LoxInstance::LoxInstance(std::shared_ptr<LoxClass> klass)
    : klass_(std::move(klass)) {}

Value LoxInstance::get(const Token& name) {
  if (const auto it = fields_.find(name.lexeme); it != fields_.end()) {
    return it->second;
  }

  auto method = klass_->findMethod(name.lexeme);
  if (method) {
    return Value(std::static_pointer_cast<LoxCallable>(
        method->bind(shared_from_this())));
  }

  throw RuntimeError(name, "Undefined property '" + name.lexeme + "'.");
}

void LoxInstance::set(const Token& name, Value value) {
  fields_[name.lexeme] = std::move(value);
}

std::string LoxInstance::toString() const {
  return klass_->name() + " instance";
}

}  // namespace loxpp
