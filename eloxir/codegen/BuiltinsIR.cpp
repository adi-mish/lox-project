#include "BuiltinsIR.h"
#include "../runtime/RuntimeAPI.h"

#include <llvm/IR/Attributes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>

namespace eloxir {

namespace {

llvm::Type *valueTy(llvm::LLVMContext &ctx) {
  return llvm::Type::getInt64Ty(ctx);
}

llvm::PointerType *i8PtrTy(llvm::LLVMContext &ctx) {
  return llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0);
}

llvm::PointerType *valuePtrTy(llvm::LLVMContext &ctx) {
  return llvm::PointerType::get(valueTy(ctx), 0);
}

llvm::Function *declare(llvm::Module &module, const char *name,
                        llvm::FunctionType *type) {
  auto callee = module.getOrInsertFunction(name, type);
  return llvm::cast<llvm::Function>(callee.getCallee());
}

void markNoUnwind(llvm::Module &module, const char *name) {
  if (auto *fn = module.getFunction(name)) {
    fn->setDoesNotThrow();
  }
}

void markReadOnly(llvm::Module &module, const char *name) {
  if (auto *fn = module.getFunction(name)) {
    fn->setDoesNotThrow();
    fn->setWillReturn();
    fn->setOnlyReadsMemory();
  }
}

} // namespace

llvm::StructType *
getOrCreatePropertyCacheEntryIRType(llvm::LLVMContext &ctx) {
  if (auto *type = llvm::StructType::getTypeByName(
          ctx, "struct.elx.PropertyCacheEntry")) {
    return type;
  }

  auto *shapePtrTy = i8PtrTy(ctx);
  auto *slotTy = llvm::Type::getInt32Ty(ctx);
  return llvm::StructType::create(ctx, {shapePtrTy, slotTy},
                                  "struct.elx.PropertyCacheEntry");
}

llvm::StructType *getOrCreatePropertyCacheIRType(llvm::LLVMContext &ctx) {
  if (auto *type =
          llvm::StructType::getTypeByName(ctx, "struct.elx.PropertyCache")) {
    return type;
  }

  auto *entryTy = getOrCreatePropertyCacheEntryIRType(ctx);
  auto *entriesArrayTy =
      llvm::ArrayType::get(entryTy, eloxir::PROPERTY_CACHE_MAX_SIZE);
  return llvm::StructType::create(ctx,
                                  {llvm::Type::getInt32Ty(ctx),
                                   entriesArrayTy},
                                  "struct.elx.PropertyCache");
}

llvm::StructType *getOrCreateCallInlineCacheIRType(llvm::LLVMContext &ctx) {
  if (auto *type =
          llvm::StructType::getTypeByName(ctx, "struct.elx.CallInlineCache")) {
    return type;
  }

  auto *i64Ty = llvm::Type::getInt64Ty(ctx);
  auto *i32Ty = llvm::Type::getInt32Ty(ctx);
  auto *ptrTy = i8PtrTy(ctx);
  return llvm::StructType::create(ctx,
                                  {i64Ty, i64Ty, i64Ty, ptrTy, i32Ty, i32Ty,
                                   i32Ty, i32Ty},
                                  "struct.elx.CallInlineCache");
}

void declareRuntimeBuiltins(llvm::Module &module) {
  auto &ctx = module.getContext();
  auto *value = valueTy(ctx);
  auto *i8Ptr = i8PtrTy(ctx);
  auto *i32 = llvm::Type::getInt32Ty(ctx);
  auto *voidTy = llvm::Type::getVoidTy(ctx);
  auto *valuePtr = valuePtrTy(ctx);
  auto *presencePtr = llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0);
  auto *cachePtr =
      llvm::PointerType::get(getOrCreatePropertyCacheIRType(ctx), 0);
  auto *callCachePtr =
      llvm::PointerType::get(getOrCreateCallInlineCacheIRType(ctx), 0);

  declare(module, "elx_print",
          llvm::FunctionType::get(value, {value}, false));
  declare(module, "elx_clock", llvm::FunctionType::get(value, {}, false));
  declare(module, "elx_readLine", llvm::FunctionType::get(value, {}, false));

  auto *stringAllocTy =
      llvm::FunctionType::get(value, {i8Ptr, i32}, false);
  declare(module, "elx_allocate_string", stringAllocTy);
  declare(module, "elx_intern_string", stringAllocTy);
  declare(module, "elx_debug_string_address",
          llvm::FunctionType::get(value, {value}, false));
  declare(module, "elx_free_object",
          llvm::FunctionType::get(voidTy, {value}, false));
  declare(module, "elx_concatenate_strings",
          llvm::FunctionType::get(value, {value, value}, false));

  auto *binaryPredicateTy =
      llvm::FunctionType::get(i32, {value, value}, false);
  declare(module, "elx_strings_equal", binaryPredicateTy);
  declare(module, "elx_strings_equal_interned", binaryPredicateTy);
  declare(module, "elx_value_is_string",
          llvm::FunctionType::get(i32, {value}, false));

  declare(module, "elx_allocate_function",
          llvm::FunctionType::get(value, {i8Ptr, i32, i8Ptr}, false));
  auto *callValueTy =
      llvm::FunctionType::get(value, {value, valuePtr, i32}, false);
  declare(module, "elx_call_function", callValueTy);
  declare(module, "elx_call_value", callValueTy);
  declare(module, "elx_call_property",
          llvm::FunctionType::get(value, {value, value, valuePtr, i32},
                                  false));
  declare(module, "elx_prepare_property_call",
          llvm::FunctionType::get(i32, {value, value, valuePtr}, false));
  declare(module, "elx_prepare_property_call_cached",
          llvm::FunctionType::get(i32, {value, value, callCachePtr, valuePtr},
                                  false));
  declare(module, "elx_call_prepared_property",
          llvm::FunctionType::get(value, {i32, value, value, valuePtr, i32},
                                  false));

  auto *valuePredicateTy = llvm::FunctionType::get(i32, {value}, false);
  declare(module, "elx_is_function", valuePredicateTy);
  declare(module, "elx_is_closure", valuePredicateTy);
  declare(module, "elx_is_native", valuePredicateTy);
  declare(module, "elx_is_class", valuePredicateTy);
  declare(module, "elx_is_bound_method", valuePredicateTy);
  declare(module, "elx_allocate_native",
          llvm::FunctionType::get(value, {i8Ptr, i32, i8Ptr}, false));
  declare(module, "elx_call_native", callValueTy);

  auto *fastCallTy =
      llvm::FunctionType::get(value, {value, valuePtr, i32, i8Ptr, i32},
                              false);
  declare(module, "elx_call_function_fast", fastCallTy);
  declare(module, "elx_call_closure_fast", fastCallTy);
  declare(module, "elx_call_native_fast", fastCallTy);
  declare(module, "elx_call_bound_method_fast",
          llvm::FunctionType::get(value,
                                  {value, valuePtr, i32, value, i8Ptr, i32,
                                   value, i32},
                                  false));
  declare(module, "elx_call_class_fast",
          llvm::FunctionType::get(value,
                                  {value, valuePtr, i32, value, i8Ptr, i32,
                                   i32},
                                  false));
  declare(module, "elx_bound_method_matches",
          llvm::FunctionType::get(i32, {value, value, value}, false));
  declare(module, "elx_call_cache_invalidate",
          llvm::FunctionType::get(voidTy, {callCachePtr}, false));
  declare(module, "elx_call_cache_update",
          llvm::FunctionType::get(voidTy, {callCachePtr, value}, false));

  declare(module, "elx_cache_stats_enabled",
          llvm::FunctionType::get(i32, {}, false));
  declare(module, "elx_cache_stats_reset",
          llvm::FunctionType::get(voidTy, {}, false));
#ifdef ELOXIR_ENABLE_CACHE_STATS
  declare(module, "elx_cache_stats_record_property_hit",
          llvm::FunctionType::get(voidTy, {i32}, false));
  declare(module, "elx_cache_stats_record_property_miss",
          llvm::FunctionType::get(voidTy, {i32}, false));
  declare(module, "elx_cache_stats_record_property_shape_transition",
          llvm::FunctionType::get(voidTy, {i32}, false));
  declare(module, "elx_cache_stats_record_call_hit",
          llvm::FunctionType::get(voidTy, {i32}, false));
  declare(module, "elx_cache_stats_record_call_miss",
          llvm::FunctionType::get(voidTy, {}, false));
  declare(module, "elx_cache_stats_record_call_transition",
          llvm::FunctionType::get(voidTy, {i32, i32}, false));
#endif

  declare(module, "elx_allocate_upvalue",
          llvm::FunctionType::get(value, {valuePtr}, false));
  declare(module, "elx_allocate_upvalue_with_value",
          llvm::FunctionType::get(value, {value}, false));
  declare(module, "elx_allocate_closure",
          llvm::FunctionType::get(value, {value, i32}, false));
  declare(module, "elx_set_closure_upvalue",
          llvm::FunctionType::get(voidTy, {value, i32, value}, false));
  declare(module, "elx_get_upvalue_value",
          llvm::FunctionType::get(value, {value}, false));
  declare(module, "elx_set_upvalue_value",
          llvm::FunctionType::get(voidTy, {value, value}, false));
  declare(module, "elx_close_upvalues",
          llvm::FunctionType::get(voidTy, {valuePtr}, false));
  declare(module, "elx_call_closure", callValueTy);

  declare(module, "elx_validate_superclass",
          llvm::FunctionType::get(value, {value}, false));
  declare(module, "elx_allocate_class",
          llvm::FunctionType::get(value, {value, value}, false));
  declare(module, "elx_class_add_method",
          llvm::FunctionType::get(voidTy, {value, value, value}, false));
  declare(module, "elx_class_prepare_field_shape",
          llvm::FunctionType::get(voidTy, {value, value}, false));
  declare(module, "elx_class_find_method",
          llvm::FunctionType::get(value, {value, value}, false));
  declare(module, "elx_instantiate_class",
          llvm::FunctionType::get(value, {value}, false));
  declare(module, "elx_instantiate_known_class",
          llvm::FunctionType::get(value, {value}, false));
  declare(module, "elx_get_instance_class",
          llvm::FunctionType::get(value, {value}, false));
  declare(module, "elx_get_instance_field",
          llvm::FunctionType::get(value, {value, value}, false));
  declare(module, "elx_set_instance_field",
          llvm::FunctionType::get(value, {value, value, value}, false));
  declare(module, "elx_set_instance_field_slot",
          llvm::FunctionType::get(value, {value, value, i32, value}, false));
  declare(module, "elx_try_get_instance_field",
          llvm::FunctionType::get(i32, {value, value, valuePtr}, false));
  declare(module, "elx_try_get_instance_field_cached",
          llvm::FunctionType::get(i32, {value, value, valuePtr, valuePtr,
                                        valuePtr},
                                  false));
  declare(module, "elx_set_instance_field_cached",
          llvm::FunctionType::get(value,
                                  {value, value, value, valuePtr, valuePtr},
                                  false));
  declare(module, "elx_bind_method",
          llvm::FunctionType::get(value, {value, value}, false));
  declare(module, "elx_get_property_slow",
          llvm::FunctionType::get(value, {value, value, cachePtr, i32},
                                  false));
  declare(module, "elx_set_property_slow",
          llvm::FunctionType::get(value, {value, value, value, cachePtr, i32},
                                  false));
  declare(module, "elx_instance_shape_ptr",
          llvm::FunctionType::get(i8Ptr, {value}, false));
  declare(module, "elx_instance_field_values_ptr",
          llvm::FunctionType::get(valuePtr, {value}, false));
  declare(module, "elx_instance_field_presence_ptr",
          llvm::FunctionType::get(presencePtr, {value}, false));

  declare(module, "elx_cleanup_all_objects",
          llvm::FunctionType::get(voidTy, {}, false));
  declare(module, "elx_get_global_builtin",
          llvm::FunctionType::get(value, {i8Ptr}, false));
  declare(module, "elx_initialize_global_builtins",
          llvm::FunctionType::get(voidTy, {}, false));
  declare(module, "elx_set_global_variable",
          llvm::FunctionType::get(voidTy, {i8Ptr, value}, false));
  declare(module, "elx_get_global_variable",
          llvm::FunctionType::get(value, {i8Ptr}, false));
  declare(module, "elx_has_global_variable",
          llvm::FunctionType::get(i32, {i8Ptr}, false));
  declare(module, "elx_set_global_function",
          llvm::FunctionType::get(voidTy, {i8Ptr, value}, false));
  declare(module, "elx_get_global_function",
          llvm::FunctionType::get(value, {i8Ptr}, false));
  declare(module, "elx_has_global_function",
          llvm::FunctionType::get(i32, {i8Ptr}, false));

  declare(module, "elx_runtime_error",
          llvm::FunctionType::get(voidTy, {i8Ptr}, false));
  declare(module, "elx_runtime_error_silent",
          llvm::FunctionType::get(voidTy, {i8Ptr}, false));
  declare(module, "elx_emit_runtime_error",
          llvm::FunctionType::get(voidTy, {}, false));
  declare(module, "elx_has_runtime_error",
          llvm::FunctionType::get(i32, {}, false));
  declare(module, "elx_clear_runtime_error",
          llvm::FunctionType::get(voidTy, {}, false));
  declare(module, "elx_safe_divide",
          llvm::FunctionType::get(value, {value, value}, false));

  for (const char *name : {
           "elx_value_is_string",
           "elx_strings_equal",
           "elx_strings_equal_interned",
           "elx_is_function",
           "elx_is_closure",
           "elx_is_native",
           "elx_is_class",
           "elx_is_bound_method",
           "elx_bound_method_matches",
           "elx_instance_shape_ptr",
           "elx_instance_field_values_ptr",
           "elx_instance_field_presence_ptr",
           "elx_has_runtime_error",
       }) {
    markReadOnly(module, name);
  }

  for (const char *name : {
           "elx_print",
           "elx_runtime_error",
           "elx_runtime_error_silent",
           "elx_emit_runtime_error",
           "elx_clear_runtime_error",
           "elx_safe_divide",
       }) {
    markNoUnwind(module, name);
  }
}

} // namespace eloxir
