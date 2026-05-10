#pragma once

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <system_error>

#include <llvm/ADT/SmallString.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>

namespace eloxir {

inline bool envFlag(const char *name) {
  if (const char *value = std::getenv(name)) {
    std::string_view flag(value);
    return flag == "1" || flag == "true" || flag == "ON" || flag == "on" ||
           flag == "True" || flag == "yes" || flag == "YES";
  }
  return false;
}

inline bool optimisationEnabled() {
  if (const char *disable = std::getenv("ELOXIR_DISABLE_OPT")) {
    std::string_view flag(disable);
    return !(flag == "1" || flag == "true" || flag == "ON" || flag == "on" ||
             flag == "True");
  }
  return true;
}

inline std::string sanitizeModuleName(std::string name) {
  if (name.empty()) {
    return "module";
  }

  for (char &ch : name) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (!std::isalnum(c) && ch != '_' && ch != '-' && ch != '.') {
      ch = '_';
    }
  }
  return name;
}

inline void dumpModuleIR(const llvm::Module &module, std::string_view phase) {
  if (envFlag("ELOXIR_PRINT_IR")) {
    llvm::errs() << "\n; ----- eloxir " << phase
                 << " IR: " << module.getModuleIdentifier() << " -----\n";
    module.print(llvm::errs(), nullptr);
  }

  const char *dir = std::getenv("ELOXIR_DUMP_IR_DIR");
  if (!dir && envFlag("ELOXIR_DUMP_IR")) {
    dir = "eloxir-ir";
  }
  if (!dir || dir[0] == '\0') {
    return;
  }

  std::error_code ec;
  llvm::sys::fs::create_directories(dir);
  llvm::SmallString<256> path(dir);
  std::string filename = sanitizeModuleName(module.getModuleIdentifier()) +
                         "." + std::string(phase) + ".ll";
  llvm::sys::path::append(path, filename);

  llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_Text);
  if (!ec) {
    module.print(out, nullptr);
  } else if (envFlag("ELOXIR_TRACE_OPT")) {
    llvm::errs() << "eloxir: failed to dump IR to " << path << ": "
                 << ec.message() << "\n";
  }
}

inline void appendOptionalPipeline(llvm::PassBuilder &passBuilder,
                                   llvm::ModulePassManager &mpm,
                                   const char *envName) {
  const char *pipeline = std::getenv(envName);
  if (!pipeline || pipeline[0] == '\0') {
    return;
  }

  llvm::ModulePassManager custom;
  if (llvm::Error err = passBuilder.parsePassPipeline(custom, pipeline)) {
    if (envFlag("ELOXIR_TRACE_OPT")) {
      llvm::errs() << "eloxir: failed to parse " << envName << "='" << pipeline
                   << "'\n";
    }
    llvm::consumeError(std::move(err));
    return;
  }

  if (envFlag("ELOXIR_TRACE_OPT")) {
    llvm::errs() << "eloxir: appended " << envName << "='" << pipeline << "'\n";
  }
  mpm.addPass(std::move(custom));
}

inline void appendPipeline(llvm::PassBuilder &passBuilder,
                           llvm::ModulePassManager &mpm,
                           std::string_view label,
                           std::string_view pipeline) {
  llvm::ModulePassManager custom;
  if (llvm::Error err = passBuilder.parsePassPipeline(custom, pipeline)) {
    if (envFlag("ELOXIR_TRACE_OPT")) {
      llvm::errs() << "eloxir: failed to parse built-in pipeline " << label
                   << "='" << pipeline << "'\n";
    }
    llvm::consumeError(std::move(err));
    return;
  }

  if (envFlag("ELOXIR_TRACE_OPT")) {
    llvm::errs() << "eloxir: appended built-in pipeline " << label << "='"
                 << pipeline << "'\n";
  }
  mpm.addPass(std::move(custom));
}

inline void runOptimisationPipeline(llvm::Module &module,
                                    llvm::TargetMachine *targetMachine) {
  dumpModuleIR(module, "preopt");

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
  appendOptionalPipeline(passBuilder, mpm, "ELOXIR_PRE_CLEANUP_PIPELINE");
  if (envFlag("ELOXIR_AGGRESSIVE_CLEANUP") &&
      !envFlag("ELOXIR_DISABLE_AGGRESSIVE_CLEANUP")) {
    appendPipeline(passBuilder, mpm, "aggressive-cleanup",
                   "inferattrs,function-attrs,globalopt,ipsccp,"
                   "function(sroa,early-cse<memssa>,aggressive-instcombine,"
                   "instcombine,simplifycfg<hoist-common-insts;"
                   "sink-common-insts>,jump-threading,"
                   "correlated-propagation,sccp,adce,gvn),"
                   "globalopt,constmerge,globaldce");
  }
  appendOptionalPipeline(passBuilder, mpm, "ELOXIR_POST_OPT_PIPELINE");
  mpm.run(module, mam);
  dumpModuleIR(module, "postopt");
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
