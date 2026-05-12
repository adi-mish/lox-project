#include "BuiltinsIR.h"
#include "../runtime/RuntimeAPI.h"
#include "../runtime/RuntimeSymbols.h"

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

llvm::PointerType *presencePtrTy(llvm::LLVMContext &ctx) {
  return llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0);
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

llvm::FunctionType *runtimeFunctionType(llvm::Module &module,
                                        RuntimeSignature signature) {
  auto &ctx = module.getContext();
  auto *value = valueTy(ctx);
  auto *i8Ptr = i8PtrTy(ctx);
  auto *i32 = llvm::Type::getInt32Ty(ctx);
  auto *voidTy = llvm::Type::getVoidTy(ctx);
  auto *valuePtr = valuePtrTy(ctx);
  auto *presencePtr = presencePtrTy(ctx);
  auto *cachePtr =
      llvm::PointerType::get(getOrCreatePropertyCacheIRType(ctx), 0);
  auto *callCachePtr =
      llvm::PointerType::get(getOrCreateCallInlineCacheIRType(ctx), 0);

  switch (signature) {
  case RuntimeSignature::Void_None:
    return llvm::FunctionType::get(voidTy, {}, false);
  case RuntimeSignature::Void_Value:
    return llvm::FunctionType::get(voidTy, {value}, false);
  case RuntimeSignature::Void_ValuePtr:
    return llvm::FunctionType::get(voidTy, {valuePtr}, false);
  case RuntimeSignature::Void_I8Ptr:
    return llvm::FunctionType::get(voidTy, {i8Ptr}, false);
  case RuntimeSignature::Void_I32:
    return llvm::FunctionType::get(voidTy, {i32}, false);
  case RuntimeSignature::Void_Value_I32:
    return llvm::FunctionType::get(voidTy, {value, i32}, false);
  case RuntimeSignature::Void_I32_I32:
    return llvm::FunctionType::get(voidTy, {i32, i32}, false);
  case RuntimeSignature::Void_Value_Value:
    return llvm::FunctionType::get(voidTy, {value, value}, false);
  case RuntimeSignature::Void_Value_Value_Value:
    return llvm::FunctionType::get(voidTy, {value, value, value}, false);
  case RuntimeSignature::Void_I8Ptr_Value:
    return llvm::FunctionType::get(voidTy, {i8Ptr, value}, false);
  case RuntimeSignature::Void_Value_I32_Value:
    return llvm::FunctionType::get(voidTy, {value, i32, value}, false);
  case RuntimeSignature::Value_None:
    return llvm::FunctionType::get(value, {}, false);
  case RuntimeSignature::Value_Value:
    return llvm::FunctionType::get(value, {value}, false);
  case RuntimeSignature::I8Ptr_Value:
    return llvm::FunctionType::get(i8Ptr, {value}, false);
  case RuntimeSignature::Value_I8Ptr:
    return llvm::FunctionType::get(value, {i8Ptr}, false);
  case RuntimeSignature::ValuePtr_Value:
    return llvm::FunctionType::get(valuePtr, {value}, false);
  case RuntimeSignature::PresencePtr_Value:
    return llvm::FunctionType::get(presencePtr, {value}, false);
  case RuntimeSignature::Value_ValuePtr:
    return llvm::FunctionType::get(value, {valuePtr}, false);
  case RuntimeSignature::Value_Value_Value:
    return llvm::FunctionType::get(value, {value, value}, false);
  case RuntimeSignature::Value_I8Ptr_I32:
    return llvm::FunctionType::get(value, {i8Ptr, i32}, false);
  case RuntimeSignature::Value_Value_I32:
    return llvm::FunctionType::get(value, {value, i32}, false);
  case RuntimeSignature::Value_Value_Value_Value:
    return llvm::FunctionType::get(value, {value, value, value}, false);
  case RuntimeSignature::Value_Value_ValuePtr_I32:
    return llvm::FunctionType::get(value, {value, valuePtr, i32}, false);
  case RuntimeSignature::Value_Value_Value_ValuePtr_I32:
    return llvm::FunctionType::get(value, {value, value, valuePtr, i32},
                                   false);
  case RuntimeSignature::Value_Value_Value_I32_Value:
    return llvm::FunctionType::get(value, {value, value, i32, value}, false);
  case RuntimeSignature::Value_Value_Value_ValuePtr_ValuePtr:
    return llvm::FunctionType::get(value,
                                   {value, value, value, valuePtr, valuePtr},
                                   false);
  case RuntimeSignature::Value_Value_Value_CachePtr_I32:
    return llvm::FunctionType::get(value, {value, value, cachePtr, i32},
                                   false);
  case RuntimeSignature::Value_Value_Value_Value_CachePtr_I32:
    return llvm::FunctionType::get(value,
                                   {value, value, value, cachePtr, i32},
                                   false);
  case RuntimeSignature::Value_I8Ptr_I32_I8Ptr:
    return llvm::FunctionType::get(value, {i8Ptr, i32, i8Ptr}, false);
  case RuntimeSignature::Value_I32_Value_Value_ValuePtr_I32:
    return llvm::FunctionType::get(value, {i32, value, value, valuePtr, i32},
                                   false);
  case RuntimeSignature::Value_Value_ValuePtr_I32_I8Ptr_I32:
    return llvm::FunctionType::get(value, {value, valuePtr, i32, i8Ptr, i32},
                                   false);
  case RuntimeSignature::
      Value_Value_ValuePtr_I32_Value_I8Ptr_I32_Value_I32:
    return llvm::FunctionType::get(
        value, {value, valuePtr, i32, value, i8Ptr, i32, value, i32}, false);
  case RuntimeSignature::Value_Value_ValuePtr_I32_Value_I8Ptr_I32_I32:
    return llvm::FunctionType::get(
        value, {value, valuePtr, i32, value, i8Ptr, i32, i32}, false);
  case RuntimeSignature::I32_None:
    return llvm::FunctionType::get(i32, {}, false);
  case RuntimeSignature::I32_Value:
    return llvm::FunctionType::get(i32, {value}, false);
  case RuntimeSignature::I32_Value_Value:
    return llvm::FunctionType::get(i32, {value, value}, false);
  case RuntimeSignature::I32_I8Ptr:
    return llvm::FunctionType::get(i32, {i8Ptr}, false);
  case RuntimeSignature::I32_Value_Value_Value:
    return llvm::FunctionType::get(i32, {value, value, value}, false);
  case RuntimeSignature::I32_Value_Value_ValuePtr:
    return llvm::FunctionType::get(i32, {value, value, valuePtr}, false);
  case RuntimeSignature::I32_Value_Value_CallCachePtr_ValuePtr:
    return llvm::FunctionType::get(i32, {value, value, callCachePtr, valuePtr},
                                   false);
  case RuntimeSignature::I32_Value_Value_ValuePtr_ValuePtr_ValuePtr:
    return llvm::FunctionType::get(i32,
                                   {value, value, valuePtr, valuePtr,
                                    valuePtr},
                                   false);
  case RuntimeSignature::Void_CallCachePtr:
    return llvm::FunctionType::get(voidTy, {callCachePtr}, false);
  case RuntimeSignature::Void_CallCachePtr_Value:
    return llvm::FunctionType::get(voidTy, {callCachePtr, value}, false);
  }
  return llvm::FunctionType::get(voidTy, {}, false);
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
  auto *descriptors = runtimeFunctionDescriptors();
  const auto count = runtimeFunctionDescriptorCount();
  for (size_t index = 0; index < count; ++index) {
    const auto &descriptor = descriptors[index];
    declare(module, descriptor.name,
            runtimeFunctionType(module, descriptor.signature));
  }

  for (size_t index = 0; index < count; ++index) {
    const auto &descriptor = descriptors[index];
    if ((descriptor.flags & RuntimeReadOnly) != 0) {
      markReadOnly(module, descriptor.name);
    } else if ((descriptor.flags & RuntimeNoUnwind) != 0) {
      markNoUnwind(module, descriptor.name);
    }
  }
}

} // namespace eloxir
