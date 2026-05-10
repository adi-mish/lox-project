#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "token.h"
#include "value.h"

namespace loxpp {

class LoxClass;

class LoxInstance final : public std::enable_shared_from_this<LoxInstance> {
 public:
  explicit LoxInstance(std::shared_ptr<LoxClass> klass);

  Value get(const Token& name);
  void set(const Token& name, Value value);
  [[nodiscard]] std::string toString() const;

 private:
  std::shared_ptr<LoxClass> klass_;
  std::unordered_map<std::string, Value> fields_;
};

}  // namespace loxpp
