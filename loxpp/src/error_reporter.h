#pragma once

#include <string_view>

#include "token.h"

namespace loxpp {

class ErrorReporter {
 public:
  void error(int line, std::string_view message);
  void error(const Token& token, std::string_view message);

  [[nodiscard]] bool hadError() const { return had_error_; }
  void resetError() { had_error_ = false; }

 private:
  void report(int line, std::string_view where, std::string_view message);

  bool had_error_ = false;
};

}  // namespace loxpp
