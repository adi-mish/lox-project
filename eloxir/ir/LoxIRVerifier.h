#pragma once

#include "LoxIR.h"

#include <string>
#include <vector>

namespace eloxir::loxir {

struct VerificationResult {
  bool ok = true;
  std::vector<std::string> errors;
};

VerificationResult verifyModule(const LoxModule &module);

} // namespace eloxir::loxir
