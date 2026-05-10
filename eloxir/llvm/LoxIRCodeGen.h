#pragma once

#include "../ir/LoxIR.h"

#include <optional>
#include <string>

namespace llvm {
class Module;
}

namespace eloxir::loxir {

std::optional<std::string> emitLoxIRModuleToLLVM(const LoxModule &loxModule,
                                                 llvm::Module &module);

} // namespace eloxir::loxir
