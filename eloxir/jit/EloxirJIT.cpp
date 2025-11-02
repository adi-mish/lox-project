#include "EloxirJIT.h"
#include "../runtime/RuntimeAPI.h"
#include "OptimisationPipeline.h"
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/Error.h>
#include <thread>

namespace eloxir {

llvm::Expected<std::unique_ptr<EloxirJIT>> EloxirJIT::Create() {
  auto jtmb = llvm::orc::JITTargetMachineBuilder::detectHost();
  if (!jtmb)
    return jtmb.takeError();
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

  // Register runtime functions explicitly
  llvm::orc::MangleAndInterner mangle(j->jit->getExecutionSession(),
                                      j->jit->getDataLayout());
  llvm::orc::SymbolMap runtimeSymbols;
  runtimeSymbols[mangle("elx_print")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&elx_print),
                                   llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_clock")] =
      llvm::orc::ExecutorSymbolDef(llvm::orc::ExecutorAddr::fromPtr(&elx_clock),
                                   llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_readLine")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_readLine),
      llvm::JITSymbolFlags::Exported);

  // String functions
  runtimeSymbols[mangle("elx_allocate_string")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_allocate_string),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_intern_string")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_intern_string),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_debug_string_address")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_debug_string_address),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_concatenate_strings")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_concatenate_strings),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_strings_equal")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_strings_equal),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_value_is_string")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_value_is_string),
          llvm::JITSymbolFlags::Exported);

  // Function functions
  runtimeSymbols[mangle("elx_allocate_function")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_allocate_function),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_call_function")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_call_function),
      llvm::JITSymbolFlags::Exported);

  // Closure and upvalue functions
  runtimeSymbols[mangle("elx_allocate_closure")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_allocate_closure),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_allocate_upvalue")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_allocate_upvalue),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_allocate_upvalue_with_value")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_allocate_upvalue_with_value),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_set_closure_upvalue")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_set_closure_upvalue),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_get_upvalue_value")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_get_upvalue_value),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_set_upvalue_value")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_set_upvalue_value),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_close_upvalues")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_close_upvalues),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_call_closure")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_call_closure),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_call_value")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_call_value),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_is_function")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_is_function),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_is_closure")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_is_closure),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_is_native")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_is_native),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_is_class")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_is_class),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_is_bound_method")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_is_bound_method),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_bound_method_matches")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_bound_method_matches),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_call_function_fast")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_call_function_fast),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_call_closure_fast")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_call_closure_fast),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_call_native_fast")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_call_native_fast),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_call_bound_method_fast")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_call_bound_method_fast),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_call_class_fast")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_call_class_fast),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_call_cache_update")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_call_cache_update),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_call_cache_invalidate")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_call_cache_invalidate),
          llvm::JITSymbolFlags::Exported);

  // Global built-ins functions
  runtimeSymbols[mangle("elx_get_global_builtin")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_get_global_builtin),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_initialize_global_builtins")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_initialize_global_builtins),
          llvm::JITSymbolFlags::Exported);

  // Global environment functions for cross-line persistence
  runtimeSymbols[mangle("elx_set_global_variable")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_set_global_variable),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_get_global_variable")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_get_global_variable),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_has_global_variable")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_has_global_variable),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_set_global_function")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_set_global_function),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_get_global_function")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_get_global_function),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_has_global_function")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_has_global_function),
          llvm::JITSymbolFlags::Exported);

  // Error handling functions
  runtimeSymbols[mangle("elx_runtime_error")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_runtime_error),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_has_runtime_error")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_has_runtime_error),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_clear_runtime_error")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_clear_runtime_error),
          llvm::JITSymbolFlags::Exported);

  // Safe arithmetic functions
  runtimeSymbols[mangle("elx_safe_divide")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_safe_divide),
      llvm::JITSymbolFlags::Exported);

  // Class and instance helpers
  runtimeSymbols[mangle("elx_validate_superclass")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_validate_superclass),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_allocate_class")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_allocate_class),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_class_add_method")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_class_add_method),
      llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_class_find_method")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_class_find_method),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_instantiate_class")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_instantiate_class),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_get_instance_class")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_get_instance_class),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_get_instance_field")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_get_instance_field),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_try_get_instance_field")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_try_get_instance_field),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_set_instance_field")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_set_instance_field),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_get_property_slow")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_get_property_slow),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_set_property_slow")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_set_property_slow),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_instance_shape_ptr")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_instance_shape_ptr),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_instance_field_values_ptr")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_instance_field_values_ptr),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_instance_field_presence_ptr")] =
      llvm::orc::ExecutorSymbolDef(
          llvm::orc::ExecutorAddr::fromPtr(&elx_instance_field_presence_ptr),
          llvm::JITSymbolFlags::Exported);
  runtimeSymbols[mangle("elx_bind_method")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&elx_bind_method),
      llvm::JITSymbolFlags::Exported);

  llvm::cantFail(j->jit->getMainJITDylib().define(
      llvm::orc::absoluteSymbols(runtimeSymbols)));

  // Optional: optimisation layer intercept
  j->jit->getIRTransformLayer().setTransform(
      [tmPtr = j->targetMachine.get()](llvm::orc::ThreadSafeModule tsm,
                                       const llvm::orc::MaterializationResponsibility &) {
        optimise(tsm, tmPtr); // see OptimisationPipeline.h
        return tsm;
      });

  return std::move(j);
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

const llvm::Triple &EloxirJIT::getTargetTriple() const {
  return targetTriple;
}

llvm::TargetMachine *EloxirJIT::getTargetMachine() const {
  return targetMachine.get();
}

} // namespace eloxir
