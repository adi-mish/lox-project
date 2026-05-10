#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/Error.h>

namespace eloxir {

inline bool optimisationEnabled() {
  if (const char *disable = std::getenv("ELOXIR_DISABLE_OPT")) {
    std::string_view flag(disable);
    return !(flag == "1" || flag == "true" || flag == "ON" || flag == "on" ||
             flag == "True");
  }
  return true;
}

inline void runOptimisationPipeline(llvm::Module &module,
                                    llvm::TargetMachine *targetMachine) {
  llvm::PipelineTuningOptions tuningOptions;
  tuningOptions.LoopInterleaving = true;
  tuningOptions.LoopVectorization = true;
  tuningOptions.SLPVectorization = true;
  tuningOptions.LoopUnrolling = true;
  tuningOptions.MergeFunctions = true;
  tuningOptions.InlinerThreshold = 550;
  tuningOptions.CallGraphProfile = true;

  llvm::PassBuilder passBuilder(targetMachine, tuningOptions);
  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;

  if (targetMachine) {
    fam.registerPass([&] { return targetMachine->getTargetIRAnalysis(); });
  }

  passBuilder.registerModuleAnalyses(mam);
  passBuilder.registerCGSCCAnalyses(cgam);
  passBuilder.registerFunctionAnalyses(fam);
  passBuilder.registerLoopAnalyses(lam);
  passBuilder.crossRegisterProxies(lam, fam, cgam, mam);

  llvm::ModulePassManager mpm =
      passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);
  llvm::ModulePassManager lateCleanup;
  if (llvm::Error err = passBuilder.parsePassPipeline(
          lateCleanup,
          "globalopt,function(instcombine,simplifycfg),globaldce")) {
    llvm::consumeError(std::move(err));
  } else {
    mpm.addPass(std::move(lateCleanup));
  }
  mpm.run(module, mam);
}

inline void optimise(llvm::orc::ThreadSafeModule &tsm,
                     llvm::TargetMachine *targetMachine) {
  if (!optimisationEnabled())
    return;

  llvm::cantFail(tsm.withModuleDo([&](llvm::Module &module) {
    runOptimisationPipeline(module, targetMachine);
    return llvm::Error::success();
  }));
}

} // namespace eloxir
