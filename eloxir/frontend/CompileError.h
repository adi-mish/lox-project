#pragma once

#include <stdexcept>
#include <string>

namespace eloxir {

class CompileError : public std::runtime_error {
public:
  explicit CompileError(const std::string &message)
      : std::runtime_error(message) {}
  explicit CompileError(const char *message) : std::runtime_error(message) {}
};

} // namespace eloxir
