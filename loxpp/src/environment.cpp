#include "environment.h"

#include <utility>

#include "runtime_error.h"

namespace loxpp {

Environment::Environment(std::shared_ptr<Environment> enclosing)
    : enclosing_(std::move(enclosing)) {}

Value Environment::get(const Token& name) const {
  if (const auto it = values_.find(name.lexeme); it != values_.end()) {
    return it->second;
  }

  if (enclosing_) {
    return enclosing_->get(name);
  }

  throw RuntimeError(name, "Undefined variable '" + name.lexeme + "'.");
}

void Environment::assign(const Token& name, Value value) {
  if (const auto it = values_.find(name.lexeme); it != values_.end()) {
    it->second = std::move(value);
    return;
  }

  if (enclosing_) {
    enclosing_->assign(name, std::move(value));
    return;
  }

  throw RuntimeError(name, "Undefined variable '" + name.lexeme + "'.");
}

void Environment::define(std::string name, Value value) {
  values_[std::move(name)] = std::move(value);
}

Value Environment::getAt(int distance, std::string_view name) const {
  const Environment* environment = ancestor(distance);
  if (const auto it = environment->values_.find(std::string(name));
      it != environment->values_.end()) {
    return it->second;
  }
  return std::monostate{};
}

void Environment::assignAt(int distance, const Token& name, Value value) {
  ancestor(distance)->values_[name.lexeme] = std::move(value);
}

const Environment* Environment::ancestor(int distance) const {
  const Environment* environment = this;
  for (int i = 0; i < distance; ++i) {
    environment = environment->enclosing_.get();
  }
  return environment;
}

Environment* Environment::ancestor(int distance) {
  Environment* environment = this;
  for (int i = 0; i < distance; ++i) {
    environment = environment->enclosing_.get();
  }
  return environment;
}

}  // namespace loxpp
