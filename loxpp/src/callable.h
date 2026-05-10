#pragma once

#include <string>
#include <vector>

#include "value.h"

namespace loxpp {

class Interpreter;

class LoxCallable {
 public:
  virtual ~LoxCallable() = default;
  [[nodiscard]] virtual int arity() const = 0;
  virtual Value call(Interpreter& interpreter, const std::vector<Value>& args) = 0;
  [[nodiscard]] virtual std::string toString() const = 0;
};

}  // namespace loxpp
