#include "EloxirJIT.h"
#include "OptimisationPipeline.h"
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/Error.h>
#include <thread>

namespace eloxir {

llvm::Expected<std::unique_ptr<EloxirJIT>> EloxirJIT::Create() {
    auto jtmb = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!jtmb) return jtmb.takeError();
    auto dl = jtmb->getDefaultDataLayoutForTarget();
    if (!dl) return dl.takeError();

    auto j = std::unique_ptr<EloxirJIT>(new EloxirJIT());
    auto builder = llvm::orc::LLJITBuilder();
    builder.setJITTargetMachineBuilder(*jtmb)
           .setDataLayout(*dl)
           .setNumCompileThreads(std::thread::hardware_concurrency());
    if (auto jitPtr = builder.create()) j->jit = std::move(*jitPtr);
    else return jitPtr.takeError();

    // Expose host C symbols to IR (print, clock, malloc, …)
    j->jit->getMainJITDylib().addGenerator(
        llvm::cantFail(llvm::orc::DynamicLibrarySearchGenerator::
                     GetForCurrentProcess(j->jit->getDataLayout().getGlobalPrefix())));

    // Optional: optimisation layer intercept
    j->jit->getIRTransformLayer().setTransform(
        [](llvm::orc::ThreadSafeModule tsm, const llvm::orc::MaterializationResponsibility&) {
            optimise(tsm);   // see OptimisationPipeline.h
            return tsm;
        });

    return std::move(j);
}

llvm::Error EloxirJIT::addModule(llvm::orc::ThreadSafeModule tsm) {
    return jit->addIRModule(std::move(tsm));
}

llvm::Expected<llvm::JITEvaluatedSymbol> EloxirJIT::lookup(std::string name) {
    return jit->lookup(name);
}

} // namespace eloxir
