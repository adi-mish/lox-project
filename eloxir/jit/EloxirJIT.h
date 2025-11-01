#pragma once
#include <llvm/TargetParser/Triple.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>

namespace eloxir {

class EloxirJIT {
  std::unique_ptr<llvm::orc::LLJIT> jit;
  llvm::Triple targetTriple;
  std::unique_ptr<llvm::TargetMachine> targetMachine;

  EloxirJIT() = default;

public:
  static llvm::Expected<std::unique_ptr<EloxirJIT>> Create();

  llvm::Error addModule(llvm::orc::ThreadSafeModule tsm);
  llvm::Expected<llvm::JITEvaluatedSymbol> lookup(std::string name);
  const llvm::DataLayout &getDataLayout() const;
  const llvm::Triple &getTargetTriple() const;
  llvm::TargetMachine *getTargetMachine() const;
};

} // namespace eloxir
