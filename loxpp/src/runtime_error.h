#pragma once

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include "token.h"
#include "value.h"

namespace loxpp {

class RuntimeError final : public std::runtime_error {
 public:
  RuntimeError(Token token, const std::string& message)
      : std::runtime_error(message), token(std::move(token)) {}

  Token token;
};

class ReturnSignal final : public std::exception {
 public:
  explicit ReturnSignal(Value value) : value(std::move(value)) {}

  const char* what() const noexcept override { return "lox return"; }

  Value value;
};

}  // namespace loxpp
