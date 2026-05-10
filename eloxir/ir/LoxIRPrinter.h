#pragma once

#include "LoxIR.h"

#include <iosfwd>
#include <string>

namespace eloxir::loxir {

void printModule(std::ostream &out, const LoxModule &module);
std::string moduleToString(const LoxModule &module);

} // namespace eloxir::loxir
