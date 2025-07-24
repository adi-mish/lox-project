#pragma once
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>

namespace eloxir {

inline void optimise(llvm::orc::ThreadSafeModule &tsm) {
  // For now, just return the module as-is without optimizations
  // TODO: Implement proper optimization pipeline when LLVM headers are
  // configured correctly
  (void)tsm; // suppress unused parameter warning
}

} // namespace eloxir
