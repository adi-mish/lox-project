#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>

namespace eloxir {

inline bool optimisationsEnabled() {
  static const bool enabled = [] {
    const char *flag = std::getenv("ELOXIR_DISABLE_OPT");
    if (!flag)
      return true;

    std::string value(flag);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });

    if (value.empty())
      return false;
    if (value == "0" || value == "false" || value == "no" || value == "off")
      return true;

    return false;
  }();
  return enabled;
}

inline void runOptimisationPipeline(llvm::Module &module) {
  if (!optimisationsEnabled())
    return;

  llvm::PassBuilder pb;
  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;

  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);

  llvm::ModulePassManager mpm =
      pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
  mpm.run(module, mam);
}

inline void optimise(llvm::orc::ThreadSafeModule &tsm) {
  if (!tsm)
    return;

  tsm.withModuleDo([](llvm::Module &module) { runOptimisationPipeline(module); });
}

} // namespace eloxir
