#include "lox_class.h"

#include <utility>

#include "interpreter.h"
#include "lox_function.h"
#include "lox_instance.h"

namespace loxpp {

LoxClass::LoxClass(std::string name,
                   std::shared_ptr<LoxClass> superclass,
                   MethodMap methods)
    : name_(std::move(name)),
      superclass_(std::move(superclass)),
      methods_(std::move(methods)) {}

std::shared_ptr<LoxFunction> LoxClass::findMethod(
    const std::string& name) const {
  if (const auto it = methods_.find(name); it != methods_.end()) {
    return it->second;
  }

  if (superclass_) {
    return superclass_->findMethod(name);
  }

  return nullptr;
}

int LoxClass::arity() const {
  auto initializer = findMethod("init");
  return initializer ? initializer->arity() : 0;
}

Value LoxClass::call(Interpreter& interpreter, const std::vector<Value>& args) {
  auto instance = std::make_shared<LoxInstance>(shared_from_this());
  auto initializer = findMethod("init");
  if (initializer) {
    initializer->bind(instance)->call(interpreter, args);
  }
  return Value(instance);
}

std::string LoxClass::toString() const {
  return name_;
}

}  // namespace loxpp
