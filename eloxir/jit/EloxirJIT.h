#pragma once
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>

namespace eloxir {

class EloxirJIT {
  std::unique_ptr<llvm::orc::LLJIT> jit;

  EloxirJIT() = default;

public:
  static llvm::Expected<std::unique_ptr<EloxirJIT>> Create();

  llvm::Error addModule(llvm::orc::ThreadSafeModule tsm);
  llvm::Expected<llvm::JITEvaluatedSymbol> lookup(std::string name);
};

} // namespace eloxir
