#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "token.h"
#include "value.h"

namespace loxpp {

class Environment {
 public:
  explicit Environment(std::shared_ptr<Environment> enclosing = nullptr);

  [[nodiscard]] Value get(const Token& name) const;
  void assign(const Token& name, Value value);
  void define(std::string name, Value value);

  [[nodiscard]] Value getAt(int distance, std::string_view name) const;
  void assignAt(int distance, const Token& name, Value value);

  [[nodiscard]] std::shared_ptr<Environment> enclosing() const {
    return enclosing_;
  }

 private:
  [[nodiscard]] const Environment* ancestor(int distance) const;
  Environment* ancestor(int distance);

  std::shared_ptr<Environment> enclosing_;
  std::unordered_map<std::string, Value> values_;
};

}  // namespace loxpp
