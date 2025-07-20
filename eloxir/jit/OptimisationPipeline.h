#pragma once
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/FunctionAnalysisManager.h>
#include <llvm/Analysis/ModuleAnalysisManager.h>

namespace eloxir {

inline void optimise(llvm::orc::ThreadSafeModule &tsm) {
    auto &ctx = *tsm.getContext();
    llvm::Module& m = *tsm.getModule();
    llvm::PassBuilder pb;
    llvm::LoopAnalysisManager     lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager    cgam;
    llvm::ModuleAnalysisManager   mam;

    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);

    // O1 equivalent with inlining turned off (de‑opts cheaper)
    auto mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O1);
    mpm.run(m, mam);
}

} // namespace eloxir
