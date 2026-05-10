#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "callable.h"

namespace loxpp {

class LoxFunction;

class LoxClass final : public LoxCallable,
                       public std::enable_shared_from_this<LoxClass> {
 public:
  using MethodMap =
      std::unordered_map<std::string, std::shared_ptr<LoxFunction>>;

  LoxClass(std::string name,
           std::shared_ptr<LoxClass> superclass,
           MethodMap methods);

  [[nodiscard]] std::shared_ptr<LoxFunction> findMethod(
      const std::string& name) const;
  [[nodiscard]] int arity() const override;
  Value call(Interpreter& interpreter, const std::vector<Value>& args) override;
  [[nodiscard]] std::string toString() const override;

  [[nodiscard]] const std::string& name() const { return name_; }

 private:
  std::string name_;
  std::shared_ptr<LoxClass> superclass_;
  MethodMap methods_;
};

}  // namespace loxpp
