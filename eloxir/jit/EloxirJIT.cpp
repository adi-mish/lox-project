#include "EloxirJIT.h"
#include "../runtime/RuntimeSymbols.h"
#include "OptimisationPipeline.h"
#include <algorithm>
#include <llvm/ADT/StringMap.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/Error.h>
#include <llvm/TargetParser/Host.h>
#include <thread>

namespace eloxir {

llvm::Expected<std::unique_ptr<EloxirJIT>> EloxirJIT::Create() {
  auto jtmb = llvm::orc::JITTargetMachineBuilder::detectHost();
  if (!jtmb)
    return jtmb.takeError();

  auto hostCpu = llvm::sys::getHostCPUName();
  if (!hostCpu.empty()) {
    jtmb->setCPU(hostCpu.str());
  }

  auto hostFeatures = llvm::sys::getHostCPUFeatures();
  if (!hostFeatures.empty()) {
    std::vector<std::string> features;
    features.reserve(hostFeatures.size());
    for (const auto &feature : hostFeatures) {
      features.push_back((feature.second ? "+" : "-") + feature.first().str());
    }
    std::sort(features.begin(), features.end());
    jtmb->addFeatures(features);
  }

  jtmb->setCodeGenOptLevel(llvm::CodeGenOptLevel::Aggressive);
  auto dl = jtmb->getDefaultDataLayoutForTarget();
  if (!dl)
    return dl.takeError();

  auto tm = jtmb->createTargetMachine();
  if (!tm)
    return tm.takeError();

  auto j = std::unique_ptr<EloxirJIT>(new EloxirJIT());
  j->targetTriple = jtmb->getTargetTriple();
  j->targetMachine = std::move(*tm);
  auto builder = llvm::orc::LLJITBuilder();
  builder.setJITTargetMachineBuilder(*jtmb)
      .setDataLayout(*dl)
      .setNumCompileThreads(std::thread::hardware_concurrency());
  if (auto jitPtr = builder.create())
    j->jit = std::move(*jitPtr);
  else
    return jitPtr.takeError();

  // Expose host C symbols to IR (print, clock, malloc, …)
  j->jit->getMainJITDylib().addGenerator(llvm::cantFail(
      llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
          j->jit->getDataLayout().getGlobalPrefix())));

  // Register runtime functions explicitly.
  llvm::orc::MangleAndInterner mangle(j->jit->getExecutionSession(),
                                      j->jit->getDataLayout());
  llvm::orc::SymbolMap runtimeSymbols;
  auto *descriptors = runtimeFunctionDescriptors();
  const auto count = runtimeFunctionDescriptorCount();
  for (size_t index = 0; index < count; ++index) {
    const auto &descriptor = descriptors[index];
    runtimeSymbols[mangle(descriptor.name)] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr(descriptor.address),
        llvm::JITSymbolFlags::Exported);
  }

  llvm::cantFail(j->jit->getMainJITDylib().define(
      llvm::orc::absoluteSymbols(runtimeSymbols)));

  // Optional: optimisation layer intercept
  j->jit->getIRTransformLayer().setTransform(
      [tmPtr = j->targetMachine.get()](
          llvm::orc::ThreadSafeModule tsm,
          const llvm::orc::MaterializationResponsibility &) {
        optimise(tsm, tmPtr); // see OptimisationPipeline.h
        return tsm;
      });

  return j;
}

llvm::Error EloxirJIT::addModule(llvm::orc::ThreadSafeModule tsm) {
  return jit->addIRModule(std::move(tsm));
}

llvm::Expected<llvm::JITEvaluatedSymbol> EloxirJIT::lookup(std::string name) {
  auto addrResult = jit->lookup(name);
  if (!addrResult)
    return addrResult.takeError();

  // Convert ExecutorAddr to JITEvaluatedSymbol
  return llvm::JITEvaluatedSymbol(addrResult->getValue(),
                                  llvm::JITSymbolFlags::Exported);
}

const llvm::DataLayout &EloxirJIT::getDataLayout() const {
  return jit->getDataLayout();
}

const llvm::Triple &EloxirJIT::getTargetTriple() const { return targetTriple; }

llvm::TargetMachine *EloxirJIT::getTargetMachine() const {
  return targetMachine.get();
}

} // namespace eloxir
