#pragma once

#include <memory>
#include <string>

#include "callable.h"
#include "environment.h"
#include "stmt.h"

namespace loxpp {

class LoxInstance;

class LoxFunction final : public LoxCallable {
 public:
  LoxFunction(FunctionStmtPtr declaration,
              std::shared_ptr<Environment> closure,
              bool is_initializer);

  [[nodiscard]] std::shared_ptr<LoxFunction> bind(
      std::shared_ptr<LoxInstance> instance) const;

  [[nodiscard]] int arity() const override;
  Value call(Interpreter& interpreter,
             const std::vector<Value>& arguments) override;
  [[nodiscard]] std::string toString() const override;

 private:
  FunctionStmtPtr declaration_;
  std::shared_ptr<Environment> closure_;
  bool is_initializer_;
};

}  // namespace loxpp
