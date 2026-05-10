#pragma once

#include <string_view>

#include "token.h"

namespace loxpp {

class RuntimeError;

class ErrorReporter {
 public:
  void error(int line, std::string_view message);
  void error(const Token& token, std::string_view message);
  void runtimeError(const RuntimeError& error);

  [[nodiscard]] bool hadError() const { return had_error_; }
  [[nodiscard]] bool hadRuntimeError() const { return had_runtime_error_; }
  void resetError() { had_error_ = false; }

 private:
  void report(int line, std::string_view where, std::string_view message);

  bool had_error_ = false;
  bool had_runtime_error_ = false;
};

}  // namespace loxpp
