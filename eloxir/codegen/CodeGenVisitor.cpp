#include "CodeGenVisitor.h"
#include "../frontend/CompileError.h"
#include "../runtime/RuntimeAPI.h"
#include "../runtime/Value.h"
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <stdexcept>
#include <iostream>

namespace eloxir {

// Constants mirrored from Value.h
static constexpr uint64_t QNAN = 0x7ff8000000000000ULL;
static constexpr uint64_t MASK_TAG = 0x7ULL << 48;

CodeGenVisitor::CodeGenVisitor(llvm::Module &m)
    : builder(m.getContext()), ctx(m.getContext()), mod(m), value(nullptr),
      currentFunction(nullptr), resolver_upvalues(nullptr),
      resolver_locals(nullptr) {
  // Declare external runtime fns
  llvm::FunctionType *printFnTy =
      llvm::FunctionType::get(llvmValueTy(), {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_print", printFnTy);

  llvm::FunctionType *clockFnTy =
      llvm::FunctionType::get(llvmValueTy(), {}, false);
  mod.getOrInsertFunction("elx_clock", clockFnTy);

  // String functions
  llvm::FunctionType *allocStringTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0),
       llvm::Type::getInt32Ty(ctx)},
      false);
  mod.getOrInsertFunction("elx_allocate_string", allocStringTy);
  mod.getOrInsertFunction("elx_intern_string", allocStringTy);

  llvm::FunctionType *concatTy = llvm::FunctionType::get(
      llvmValueTy(), {llvmValueTy(), llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_concatenate_strings", concatTy);

  llvm::FunctionType *strEqualTy = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(ctx), {llvmValueTy(), llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_strings_equal", strEqualTy);

  llvm::FunctionType *isStringFnTy = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(ctx), {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_value_is_string", isStringFnTy);

  // Function functions
  llvm::FunctionType *allocFuncTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0),
       llvm::Type::getInt32Ty(ctx),
       llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0)},
      false);
  mod.getOrInsertFunction("elx_allocate_function", allocFuncTy);

  llvm::FunctionType *callFuncTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvmValueTy(), llvm::PointerType::get(llvmValueTy(), 0),
       llvm::Type::getInt32Ty(ctx)},
      false);
  mod.getOrInsertFunction("elx_call_value", callFuncTy);

  // Closure and upvalue functions
  llvm::FunctionType *allocUpvalueTy = llvm::FunctionType::get(
      llvmValueTy(), {llvm::PointerType::get(llvmValueTy(), 0)}, false);
  mod.getOrInsertFunction("elx_allocate_upvalue", allocUpvalueTy);

  llvm::FunctionType *allocClosureTy = llvm::FunctionType::get(
      llvmValueTy(), {llvmValueTy(), llvm::Type::getInt32Ty(ctx)}, false);
  mod.getOrInsertFunction("elx_allocate_closure", allocClosureTy);

  llvm::FunctionType *setClosureUpvalueTy = llvm::FunctionType::get(
      llvm::Type::getVoidTy(ctx),
      {llvmValueTy(), llvm::Type::getInt32Ty(ctx), llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_set_closure_upvalue", setClosureUpvalueTy);

  llvm::FunctionType *getUpvalueValueTy =
      llvm::FunctionType::get(llvmValueTy(), {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_get_upvalue_value", getUpvalueValueTy);

  llvm::FunctionType *setUpvalueValueTy = llvm::FunctionType::get(
      llvm::Type::getVoidTy(ctx), {llvmValueTy(), llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_set_upvalue_value", setUpvalueValueTy);

  llvm::FunctionType *closeUpvaluesTy = llvm::FunctionType::get(
      llvm::Type::getVoidTy(ctx), {llvm::PointerType::get(llvmValueTy(), 0)},
      false);
  mod.getOrInsertFunction("elx_close_upvalues", closeUpvaluesTy);

  // Global built-ins functions
  llvm::FunctionType *getBuiltinTy = llvm::FunctionType::get(
      llvmValueTy(), {llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0)},
      false);
  mod.getOrInsertFunction("elx_get_global_builtin", getBuiltinTy);

  llvm::FunctionType *initBuiltinsTy =
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {}, false);
  mod.getOrInsertFunction("elx_initialize_global_builtins", initBuiltinsTy);

  // Class and instance helpers
  llvm::FunctionType *validateSuperTy =
      llvm::FunctionType::get(llvmValueTy(), {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_validate_superclass", validateSuperTy);

  llvm::FunctionType *allocateClassTy =
      llvm::FunctionType::get(llvmValueTy(), {llvmValueTy(), llvmValueTy()},
                              false);
  mod.getOrInsertFunction("elx_allocate_class", allocateClassTy);

  llvm::FunctionType *classAddMethodTy =
      llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                              {llvmValueTy(), llvmValueTy(), llvmValueTy()},
                              false);
  mod.getOrInsertFunction("elx_class_add_method", classAddMethodTy);

  llvm::FunctionType *classFindMethodTy =
      llvm::FunctionType::get(llvmValueTy(), {llvmValueTy(), llvmValueTy()},
                              false);
  mod.getOrInsertFunction("elx_class_find_method", classFindMethodTy);

  llvm::FunctionType *instantiateClassTy =
      llvm::FunctionType::get(llvmValueTy(), {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_instantiate_class", instantiateClassTy);

  llvm::FunctionType *getInstanceClassTy =
      llvm::FunctionType::get(llvmValueTy(), {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_get_instance_class", getInstanceClassTy);

  llvm::FunctionType *getInstanceFieldTy =
      llvm::FunctionType::get(llvmValueTy(), {llvmValueTy(), llvmValueTy()},
                              false);
  mod.getOrInsertFunction("elx_get_instance_field", getInstanceFieldTy);

  llvm::FunctionType *tryGetInstanceFieldTy = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(ctx),
      {llvmValueTy(), llvmValueTy(),
       llvm::PointerType::get(llvmValueTy(), 0)},
      false);
  mod.getOrInsertFunction("elx_try_get_instance_field", tryGetInstanceFieldTy);

  llvm::FunctionType *setInstanceFieldTy = llvm::FunctionType::get(
      llvmValueTy(), {llvmValueTy(), llvmValueTy(), llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_set_instance_field", setInstanceFieldTy);

  llvm::FunctionType *bindMethodTy = llvm::FunctionType::get(
      llvmValueTy(), {llvmValueTy(), llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_bind_method", bindMethodTy);

  // Global environment functions for cross-line persistence
  auto i8PtrTy = llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0);
  auto i32Ty = llvm::Type::getInt32Ty(ctx);
  auto voidTy = llvm::Type::getVoidTy(ctx);

  auto cacheTy = getPropertyCacheType();
  auto cachePtrTy = llvm::PointerType::get(cacheTy, 0);
  auto shapePtrTy = i8PtrTy;
  auto valuePtrTy = llvm::PointerType::get(llvmValueTy(), 0);
  auto presencePtrTy = llvm::PointerType::get(builder.getInt8Ty(), 0);
  auto callCacheTy = getCallCacheType();
  auto callCachePtrTy = llvm::PointerType::get(callCacheTy, 0);

  llvm::FunctionType *callCacheUpdateTy = llvm::FunctionType::get(
      voidTy, {callCachePtrTy, llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_call_cache_update", callCacheUpdateTy);

#ifdef ELOXIR_ENABLE_CACHE_STATS
  llvm::FunctionType *propertyHitTy =
      llvm::FunctionType::get(voidTy, {i32Ty}, false);
  mod.getOrInsertFunction("elx_cache_stats_record_property_hit", propertyHitTy);

  llvm::FunctionType *propertyMissTy =
      llvm::FunctionType::get(voidTy, {i32Ty}, false);
  mod.getOrInsertFunction("elx_cache_stats_record_property_miss", propertyMissTy);

  llvm::FunctionType *callHitTy =
      llvm::FunctionType::get(voidTy, {i32Ty}, false);
  mod.getOrInsertFunction("elx_cache_stats_record_call_hit", callHitTy);

  llvm::FunctionType *callMissTy =
      llvm::FunctionType::get(voidTy, {}, false);
  mod.getOrInsertFunction("elx_cache_stats_record_call_miss", callMissTy);
#endif

  llvm::FunctionType *isNativeTy =
      llvm::FunctionType::get(i32Ty, {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_is_function", isNativeTy);
  mod.getOrInsertFunction("elx_is_closure", isNativeTy);
  mod.getOrInsertFunction("elx_is_native", isNativeTy);
  mod.getOrInsertFunction("elx_is_class", isNativeTy);
  mod.getOrInsertFunction("elx_is_bound_method", isNativeTy);

  llvm::FunctionType *boundMatchesTy = llvm::FunctionType::get(
      i32Ty, {llvmValueTy(), llvmValueTy(), llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_bound_method_matches", boundMatchesTy);

  llvm::FunctionType *callFunctionFastTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvmValueTy(), valuePtrTy, i32Ty, i8PtrTy, i32Ty}, false);
  mod.getOrInsertFunction("elx_call_function_fast", callFunctionFastTy);

  llvm::FunctionType *callClosureFastTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvmValueTy(), valuePtrTy, i32Ty, i8PtrTy, i32Ty}, false);
  mod.getOrInsertFunction("elx_call_closure_fast", callClosureFastTy);

  llvm::FunctionType *callNativeFastTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvmValueTy(), valuePtrTy, i32Ty, i8PtrTy, i32Ty}, false);
  mod.getOrInsertFunction("elx_call_native_fast", callNativeFastTy);

  llvm::FunctionType *callBoundFastTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvmValueTy(), valuePtrTy, i32Ty, llvmValueTy(), i8PtrTy, i32Ty,
       llvmValueTy(), i32Ty},
      false);
  mod.getOrInsertFunction("elx_call_bound_method_fast", callBoundFastTy);

  llvm::FunctionType *callClassFastTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvmValueTy(), valuePtrTy, i32Ty, llvmValueTy(), i8PtrTy, i32Ty, i32Ty},
      false);
  mod.getOrInsertFunction("elx_call_class_fast", callClassFastTy);

  llvm::FunctionType *instanceShapePtrTy =
      llvm::FunctionType::get(shapePtrTy, {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_instance_shape_ptr", instanceShapePtrTy);

  llvm::FunctionType *instanceFieldsPtrTy =
      llvm::FunctionType::get(valuePtrTy, {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_instance_field_values_ptr",
                          instanceFieldsPtrTy);

  llvm::FunctionType *instancePresencePtrTy =
      llvm::FunctionType::get(presencePtrTy, {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_instance_field_presence_ptr",
                          instancePresencePtrTy);

  llvm::FunctionType *getPropertySlowTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvmValueTy(), llvmValueTy(), cachePtrTy, llvm::Type::getInt32Ty(ctx)},
      false);
  mod.getOrInsertFunction("elx_get_property_slow", getPropertySlowTy);

  llvm::FunctionType *setPropertySlowTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvmValueTy(), llvmValueTy(), llvmValueTy(), cachePtrTy,
       llvm::Type::getInt32Ty(ctx)},
      false);
  mod.getOrInsertFunction("elx_set_property_slow", setPropertySlowTy);

  llvm::FunctionType *hasGlobalVarTy =
      llvm::FunctionType::get(i32Ty, {i8PtrTy}, false);
  mod.getOrInsertFunction("elx_has_global_variable", hasGlobalVarTy);

  llvm::FunctionType *getGlobalVarTy =
      llvm::FunctionType::get(llvmValueTy(), {i8PtrTy}, false);
  mod.getOrInsertFunction("elx_get_global_variable", getGlobalVarTy);

  llvm::FunctionType *setGlobalVarTy =
      llvm::FunctionType::get(voidTy, {i8PtrTy, llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_set_global_variable", setGlobalVarTy);

  llvm::FunctionType *hasGlobalFuncTy =
      llvm::FunctionType::get(i32Ty, {i8PtrTy}, false);
  mod.getOrInsertFunction("elx_has_global_function", hasGlobalFuncTy);

  llvm::FunctionType *getGlobalFuncTy =
      llvm::FunctionType::get(llvmValueTy(), {i8PtrTy}, false);
  mod.getOrInsertFunction("elx_get_global_function", getGlobalFuncTy);

  llvm::FunctionType *setGlobalFuncTy =
      llvm::FunctionType::get(voidTy, {i8PtrTy, llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_set_global_function", setGlobalFuncTy);

  // Runtime error functions
  llvm::FunctionType *runtimeErrorTy =
      llvm::FunctionType::get(voidTy, {i8PtrTy}, false);
  mod.getOrInsertFunction("elx_runtime_error", runtimeErrorTy);

  llvm::FunctionType *hasRuntimeErrorTy =
      llvm::FunctionType::get(i32Ty, {}, false);
  mod.getOrInsertFunction("elx_has_runtime_error", hasRuntimeErrorTy);

  llvm::FunctionType *clearRuntimeErrorTy =
      llvm::FunctionType::get(voidTy, {}, false);
  mod.getOrInsertFunction("elx_clear_runtime_error", clearRuntimeErrorTy);

  // Safe arithmetic functions
  llvm::FunctionType *safeDivideTy = llvm::FunctionType::get(
      llvmValueTy(), {llvmValueTy(), llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_safe_divide", safeDivideTy);

  // Built-ins will be initialized when first generating code
}

llvm::StructType *CodeGenVisitor::getPropertyCacheType() {
  if (propertyCacheTy)
    return propertyCacheTy;

  auto shapePtrTy = llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0);
  auto slotTy = llvm::Type::getInt32Ty(ctx);
  static llvm::StructType *entryTy = nullptr;
  if (!entryTy) {
    entryTy = llvm::StructType::create(
        ctx, {shapePtrTy, slotTy}, "struct.elx.PropertyCacheEntry");
  }
  auto entriesArrayTy =
      llvm::ArrayType::get(entryTy, PROPERTY_CACHE_MAX_SIZE);
  std::vector<llvm::Type *> elements = {llvm::Type::getInt32Ty(ctx),
                                        entriesArrayTy};
  propertyCacheTy = llvm::StructType::create(ctx, elements,
                                             "struct.elx.PropertyCache");
  return propertyCacheTy;
}

llvm::GlobalVariable *CodeGenVisitor::getPropertyCacheGlobal(
    const std::string &prefix, const Expr *expr) {
  auto it = propertyCacheGlobals.find(expr);
  if (it != propertyCacheGlobals.end()) {
    return it->second;
  }

  auto cacheTy = getPropertyCacheType();
  auto zeroInit = llvm::Constant::getNullValue(cacheTy);
  std::string name = prefix + ".cache." +
                     std::to_string(reinterpret_cast<uintptr_t>(expr));

  auto *global = new llvm::GlobalVariable(
      mod, cacheTy, false, llvm::GlobalValue::InternalLinkage, zeroInit, name);
  propertyCacheGlobals[expr] = global;
  return global;
}

llvm::StructType *CodeGenVisitor::getCallCacheType() {
  if (callCacheTy)
    return callCacheTy;

  auto i64Ty = llvm::Type::getInt64Ty(ctx);
  auto i8PtrTy = llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0);
  auto i32Ty = llvm::Type::getInt32Ty(ctx);
  std::vector<llvm::Type *> elements = {i64Ty, i64Ty, i64Ty, i8PtrTy,
                                        i32Ty, i32Ty, i32Ty, i32Ty};
  callCacheTy =
      llvm::StructType::create(ctx, elements, "struct.elx.CallInlineCache");
  return callCacheTy;
}

llvm::GlobalVariable *CodeGenVisitor::getCallCacheGlobal(
    const std::string &prefix, const Expr *expr) {
  auto it = callCacheGlobals.find(expr);
  if (it != callCacheGlobals.end()) {
    return it->second;
  }

  auto cacheTy = getCallCacheType();
  auto zeroInit = llvm::Constant::getNullValue(cacheTy);
  std::string name = prefix + ".callcache." +
                     std::to_string(reinterpret_cast<uintptr_t>(expr));

  auto *global = new llvm::GlobalVariable(
      mod, cacheTy, false, llvm::GlobalValue::InternalLinkage, zeroInit, name);
  callCacheGlobals[expr] = global;
  return global;
}

std::size_t CodeGenVisitor::saturatingLoopAdd(std::size_t current,
                                              std::size_t increment) const {
  constexpr std::size_t sentinel = MAX_LOOP_BODY_INSTRUCTIONS + 1;
  if (current >= sentinel) {
    return sentinel;
  }
  if (increment >= sentinel) {
    return sentinel;
  }
  if (increment > sentinel - current) {
    return sentinel;
  }
  std::size_t total = current + increment;
  if (total >= sentinel) {
    return sentinel;
  }
  return total;
}

std::size_t CodeGenVisitor::estimateLoopBodyInstructions(Stmt *stmt) const {
  if (!stmt) {
    return 0;
  }

  if (auto *block = dynamic_cast<Block *>(stmt)) {
    std::size_t total = 0;
    for (const auto &inner : block->statements) {
      total = saturatingLoopAdd(total,
                                estimateLoopBodyInstructions(inner.get()));
      if (total > MAX_LOOP_BODY_INSTRUCTIONS) {
        return total;
      }
    }
    return total;
  }

  if (auto *whileStmt = dynamic_cast<While *>(stmt)) {
    // Roughly approximate the extra jump instructions emitted for a loop by
    // counting the body plus a small constant for the condition/back-edge.
    std::size_t body = estimateLoopBodyInstructions(whileStmt->body.get());
    return saturatingLoopAdd(4, body);
  }

  if (auto *ifStmt = dynamic_cast<If *>(stmt)) {
    std::size_t thenCount = estimateLoopBodyInstructions(ifStmt->thenBranch.get());
    std::size_t elseCount = estimateLoopBodyInstructions(ifStmt->elseBranch.get());
    return saturatingLoopAdd(1, std::max(thenCount, elseCount));
  }

  if (dynamic_cast<Expression *>(stmt) != nullptr) {
    return 2;
  }

  if (dynamic_cast<Print *>(stmt) != nullptr) {
    return 2;
  }

  if (dynamic_cast<Var *>(stmt) != nullptr) {
    return 2;
  }

  if (dynamic_cast<Return *>(stmt) != nullptr) {
    return 2;
  }

  if (dynamic_cast<Class *>(stmt) != nullptr) {
    return 4;
  }

  if (dynamic_cast<Function *>(stmt) != nullptr) {
    return 4;
  }

  return 1;
}

llvm::Type *CodeGenVisitor::llvmValueTy() const {
  return llvm::Type::getInt64Ty(ctx);
}

// --------- Helpers -------------------------------------------------------
llvm::Value *CodeGenVisitor::tagOf(llvm::Value *v) {
  auto mask = llvm::ConstantInt::get(llvmValueTy(), MASK_TAG);
  return builder.CreateAnd(v, mask, "tag");
}

llvm::Value *CodeGenVisitor::isNumber(llvm::Value *v) {
  // IEEE 754 compliant logic:
  // A value is a number if it's NOT our special QNAN-boxed non-number types
  // This includes normal numbers, infinity, and even NaN values that aren't our
  // tagged types

  auto qnanMask = llvm::ConstantInt::get(llvmValueTy(), 0xfff8000000000000ULL);
  auto qnanPattern =
      llvm::ConstantInt::get(llvmValueTy(), 0x7ff8000000000000ULL);
  auto zero = llvm::ConstantInt::get(llvmValueTy(), 0);

  // Check if it matches our QNAN pattern used for tagging
  auto qnanMasked = builder.CreateAnd(v, qnanMask, "qnanmasked");
  auto isOurQNaN = builder.CreateICmpEQ(qnanMasked, qnanPattern, "isourqnan");

  // If it's our QNAN, check if tag bits are 0 (number type)
  auto tagBits = tagOf(v);
  auto hasZeroTag = builder.CreateICmpEQ(tagBits, zero, "zerotag");
  auto isTaggedNumber =
      builder.CreateAnd(isOurQNaN, hasZeroTag, "taggednumber");

  // If it's not our special QNAN pattern, it's a regular IEEE 754 number
  auto isNotOurQNaN = builder.CreateNot(isOurQNaN, "notourqnan");

  // It's a number if: not our QNAN OR (our QNAN with zero tag)
  return builder.CreateOr(isNotOurQNaN, isTaggedNumber, "isnum");
}

llvm::Value *CodeGenVisitor::toDouble(llvm::Value *v) {
  // bitcast i64 -> double
  return builder.CreateBitCast(v, llvm::Type::getDoubleTy(ctx), "asdouble");
}

llvm::Value *CodeGenVisitor::fromDouble(llvm::Value *d) {
  // bitcast double -> i64
  return builder.CreateBitCast(d, llvmValueTy(), "fromdouble");
}

llvm::Value *CodeGenVisitor::isFalsy(llvm::Value *v) {
  auto tag = tagOf(v);
  auto boolTag = llvm::ConstantInt::get(
      llvmValueTy(), (static_cast<uint64_t>(Tag::BOOL) << 48));
  auto nilTag = llvm::ConstantInt::get(llvmValueTy(),
                                       (static_cast<uint64_t>(Tag::NIL) << 48));

  auto isBool = builder.CreateICmpEQ(tag, boolTag, "isBool");
  auto isNil = builder.CreateICmpEQ(tag, nilTag, "isNil");

  auto lowBit64 = builder.CreateAnd(v, llvm::ConstantInt::get(llvmValueTy(), 1),
                                    "lowbit64");
  auto lowBit = builder.CreateTrunc(lowBit64, builder.getInt1Ty(), "lowbit");

  auto isFalseBool = builder.CreateAnd(
      isBool, builder.CreateICmpEQ(lowBit, builder.getFalse()));
  return builder.CreateOr(isFalseBool, isNil, "isFalsy");
}

llvm::Value *CodeGenVisitor::isTruthy(llvm::Value *v) {
  return builder.CreateNot(isFalsy(v), "isTruthy");
}

llvm::Value *CodeGenVisitor::boolConst(bool b) {
  uint64_t bits =
      QNAN | (static_cast<uint64_t>(Tag::BOOL) << 48) | (b ? 1ULL : 0ULL);
  return llvm::ConstantInt::get(llvmValueTy(), bits);
}

llvm::Value *CodeGenVisitor::nilConst() {
  uint64_t bits = QNAN | (static_cast<uint64_t>(Tag::NIL) << 48);
  return llvm::ConstantInt::get(llvmValueTy(), bits);
}

llvm::Value *CodeGenVisitor::makeBool(llvm::Value *i1) {
  // Convert i1 to Value representation
  // Boolean values are: QNAN | (Tag::BOOL << 48) | (0 or 1)
  auto qnanVal =
      llvm::ConstantInt::get(llvmValueTy(), 0x7ff8000000000000ULL); // QNAN
  auto tagVal = llvm::ConstantInt::get(
      llvmValueTy(), (static_cast<uint64_t>(Tag::BOOL) << 48));
  auto extended = builder.CreateZExt(i1, llvmValueTy(), "extend");
  auto withTag = builder.CreateOr(qnanVal, tagVal, "qnan_tag");
  return builder.CreateOr(withTag, extended, "bool");
}

llvm::AllocaInst *CodeGenVisitor::createStackAlloca(llvm::Function *fn,
                                                    llvm::Type *type,
                                                    const std::string &name) {
  auto &entry = fn->getEntryBlock();
  llvm::IRBuilder<> allocaBuilder(ctx);

  auto lastAllocaIt = lastAllocaForFunction.find(fn);
  if (lastAllocaIt != lastAllocaForFunction.end() &&
      lastAllocaIt->second != nullptr) {
    llvm::Instruction *lastAlloca = lastAllocaIt->second;
    if (auto *next = lastAlloca->getNextNode()) {
      allocaBuilder.SetInsertPoint(next);
    } else {
      allocaBuilder.SetInsertPoint(&entry, entry.end());
    }
  } else {
    auto firstInsertionPt = entry.getFirstInsertionPt();
    if (firstInsertionPt == entry.end()) {
      allocaBuilder.SetInsertPoint(&entry, entry.end());
    } else {
      allocaBuilder.SetInsertPoint(&entry, firstInsertionPt);
    }
  }

  auto *slot = allocaBuilder.CreateAlloca(type, nullptr, name.c_str());
  lastAllocaForFunction[fn] = slot;
  return slot;
}

llvm::Value *CodeGenVisitor::isString(llvm::Value *v) {
  auto objTag = llvm::ConstantInt::get(llvmValueTy(),
                                       (static_cast<uint64_t>(Tag::OBJ) << 48));
  auto tag = tagOf(v);
  auto isObj = builder.CreateICmpEQ(tag, objTag, "isobj.str");

  auto isStringFn = mod.getFunction("elx_value_is_string");
  if (!isStringFn) {
    return builder.getFalse();
  }

  auto call = builder.CreateCall(isStringFn, {v}, "isstring.call");
  auto asBool = builder.CreateICmpNE(
      call, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0),
      "isstring.bool");

  return builder.CreateAnd(isObj, asBool, "isstr");
}

llvm::Value *CodeGenVisitor::stringConst(const std::string &str,
                                         bool countAsConstant) {
  if (countAsConstant) {
    recordConstant();
  }

  // Use global string interning instead of local interning
  auto strConstant = builder.CreateGlobalStringPtr(str, "str");
  auto lengthConst =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), str.length());

  // Call elx_intern_string for global interning
  auto internFn = mod.getFunction("elx_intern_string");
  if (!internFn) {
    // Fallback to nil if function not found
    return nilConst();
  }

  auto strObj =
      builder.CreateCall(internFn, {strConstant, lengthConst}, "strobj");

  return strObj;
}

// Helper function for proper equality comparison following Lox semantics
llvm::Value *CodeGenVisitor::valuesEqual(llvm::Value *L, llvm::Value *R) {
  auto tagL = tagOf(L);
  auto tagR = tagOf(R);

  // Different types are never equal
  auto sameType = builder.CreateICmpEQ(tagL, tagR, "sametype");

  auto fn = builder.GetInsertBlock()->getParent();
  auto sameTypeBB = llvm::BasicBlock::Create(ctx, "sametype", fn);
  auto diffTypeBB = llvm::BasicBlock::Create(ctx, "difftype", fn);
  auto contBB = llvm::BasicBlock::Create(ctx, "eq.cont", fn);

  builder.CreateCondBr(sameType, sameTypeBB, diffTypeBB);

  // Different types - return false
  builder.SetInsertPoint(diffTypeBB);
  builder.CreateBr(contBB);

  // Same types - compare based on type
  builder.SetInsertPoint(sameTypeBB);
  auto numTag = llvm::ConstantInt::get(
      llvmValueTy(), static_cast<uint64_t>(Tag::NUMBER) << 48);
  auto objTag = llvm::ConstantInt::get(llvmValueTy(),
                                       static_cast<uint64_t>(Tag::OBJ) << 48);

  auto isNumBB = llvm::BasicBlock::Create(ctx, "eq.num", fn);
  auto isObjBB = llvm::BasicBlock::Create(ctx, "eq.obj", fn);
  auto isBoolOrNilBB = llvm::BasicBlock::Create(ctx, "eq.boolnil", fn);
  auto checkObjBB = llvm::BasicBlock::Create(ctx, "check_obj", fn);

  auto isNum = builder.CreateICmpEQ(tagL, numTag, "isnum");
  auto isObj = builder.CreateICmpEQ(tagL, objTag, "isobj");

  // Create a switch-like structure
  builder.CreateCondBr(isNum, isNumBB, checkObjBB);

  // Check obj branch
  builder.SetInsertPoint(checkObjBB);
  builder.CreateCondBr(isObj, isObjBB, isBoolOrNilBB);

  // Numbers: use floating-point comparison (handles NaN correctly)
  builder.SetInsertPoint(isNumBB);
  auto Ld = toDouble(L);
  auto Rd = toDouble(R);
  auto numEqual = builder.CreateFCmpOEQ(Ld, Rd, "numeq");
  builder.CreateBr(contBB);

  // Objects: compare strings structurally and other objects by identity
  builder.SetInsertPoint(isObjBB);
  auto stringsBB = llvm::BasicBlock::Create(ctx, "eq.str", fn);
  auto objPtrBB = llvm::BasicBlock::Create(ctx, "eq.objptr", fn);
  auto bothStrings = builder.CreateAnd(isString(L), isString(R), "eq.bothstr");
  builder.CreateCondBr(bothStrings, stringsBB, objPtrBB);

  builder.SetInsertPoint(stringsBB);
  llvm::Value *stringEqualBool = builder.getFalse();
  if (auto strEqualFn = mod.getFunction("elx_strings_equal")) {
    auto strEqual = builder.CreateCall(strEqualFn, {L, R}, "streq");
    stringEqualBool = builder.CreateICmpNE(
        strEqual, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0),
        "streqbool");
  }
  builder.CreateBr(contBB);
  auto stringsResBB = builder.GetInsertBlock();

  builder.SetInsertPoint(objPtrBB);
  auto objEqualBool = builder.CreateICmpEQ(L, R, "objeq");
  builder.CreateBr(contBB);
  auto objPtrResBB = builder.GetInsertBlock();

  // For bool/nil, do bitwise comparison
  builder.SetInsertPoint(isBoolOrNilBB);
  auto bitsEqual = builder.CreateICmpEQ(L, R, "bitseq");
  builder.CreateBr(contBB);

  // Merge results
  builder.SetInsertPoint(contBB);
  auto phi = builder.CreatePHI(builder.getInt1Ty(), 5, "eq.res");
  phi->addIncoming(builder.getFalse(), diffTypeBB);
  phi->addIncoming(numEqual, isNumBB);
  phi->addIncoming(stringEqualBool, stringsResBB);
  phi->addIncoming(objEqualBool, objPtrResBB);
  phi->addIncoming(bitsEqual, isBoolOrNilBB);

  return phi;
}

// Helper to check if both values are numbers and generate runtime error if not
llvm::Value *CodeGenVisitor::checkBothNumbers(llvm::Value *L, llvm::Value *R,
                                              llvm::BasicBlock *&successBB,
                                              llvm::BasicBlock *&errorBB) {
  auto fn = builder.GetInsertBlock()->getParent();
  successBB = llvm::BasicBlock::Create(ctx, "both_numbers", fn);
  errorBB = llvm::BasicBlock::Create(ctx, "type_error", fn);

  auto isLNum = isNumber(L);
  auto isRNum = isNumber(R);
  auto both = builder.CreateAnd(isLNum, isRNum, "bothnum");

  builder.CreateCondBr(both, successBB, errorBB);

  // Error case - generate runtime error instead of trap
  builder.SetInsertPoint(errorBB);
  emitRuntimeError("Operands must be numbers.");
  // Don't return here - let the caller handle control flow

  return both;
}

void CodeGenVisitor::emitRuntimeError(const std::string &message) {
  auto runtimeErrorFn = mod.getFunction("elx_runtime_error");
  if (!runtimeErrorFn)
    return;
  auto msgPtr = builder.CreateGlobalStringPtr(message, "runtime_error_msg");
  builder.CreateCall(runtimeErrorFn, {msgPtr});
}

// --------- Expr visitors -------------------------------------------------
void CodeGenVisitor::visitBinaryExpr(Binary *e) {
  e->left->accept(this);
  llvm::Value *L = value;
  e->right->accept(this);
  llvm::Value *R = value;

  auto fn = builder.GetInsertBlock()->getParent();

  // Handle equality operators separately (they work on any types)
  if (e->op.getType() == TokenType::EQUAL_EQUAL) {
    auto equal = valuesEqual(L, R);
    value = makeBool(equal);
    return;
  }

  if (e->op.getType() == TokenType::BANG_EQUAL) {
    auto equal = valuesEqual(L, R);
    auto notEqual = builder.CreateNot(equal, "ne");
    value = makeBool(notEqual);
    return;
  }

  // Handle PLUS specially - it can be number addition or string concatenation
  if (e->op.getType() == TokenType::PLUS) {
    auto bothAreNumbers =
        builder.CreateAnd(isNumber(L), isNumber(R), "bothnum");

    auto isNumAddBB = llvm::BasicBlock::Create(ctx, "plus.numadd", fn);
    auto isStrConcatBB = llvm::BasicBlock::Create(ctx, "plus.strconcat", fn);
    auto errorBB = llvm::BasicBlock::Create(ctx, "plus.error", fn);
    auto contBB = llvm::BasicBlock::Create(ctx, "plus.cont", fn);

    // Check if both are numbers first
    auto checkStrLeftBB =
        llvm::BasicBlock::Create(ctx, "plus.checkstr.left", fn);
    auto checkStrRightBB =
        llvm::BasicBlock::Create(ctx, "plus.checkstr.right", fn);
    builder.CreateCondBr(bothAreNumbers, isNumAddBB, checkStrLeftBB);

    // Validate left operand is a string object
    builder.SetInsertPoint(checkStrLeftBB);
    auto leftIsString = isString(L);
    builder.CreateCondBr(leftIsString, checkStrRightBB, errorBB);

    // Validate right operand is a string object
    builder.SetInsertPoint(checkStrRightBB);
    auto rightIsString = isString(R);
    builder.CreateCondBr(rightIsString, isStrConcatBB, errorBB);

    // Number addition
    builder.SetInsertPoint(isNumAddBB);
    auto Ld = toDouble(L);
    auto Rd = toDouble(R);
    auto numResult = fromDouble(builder.CreateFAdd(Ld, Rd, "add"));
    builder.CreateBr(contBB);

    // String concatenation
    builder.SetInsertPoint(isStrConcatBB);
    auto concatFn = mod.getFunction("elx_concatenate_strings");
    auto strResult = builder.CreateCall(concatFn, {L, R}, "concat");

    // Check for runtime error after concatenation
    llvm::Function *hasErrorFn = mod.getFunction("elx_has_runtime_error");
    llvm::Value *finalStrResult = strResult;
    if (hasErrorFn) {
      auto hasError = builder.CreateCall(hasErrorFn, {}, "has_error");
      auto hasErrorBool = builder.CreateICmpNE(
          hasError, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0),
          "error_check");

      // If there's an error, use nil instead of the result
      finalStrResult = builder.CreateSelect(hasErrorBool, nilConst(), strResult,
                                            "safe_concat");
    }

    builder.CreateBr(contBB);

    // Type error
    builder.SetInsertPoint(errorBB);
    emitRuntimeError("Operands must be numbers or strings for +.");
    auto errorResult = nilConst();
    builder.CreateBr(contBB);

    // Merge results
    builder.SetInsertPoint(contBB);
    auto phi = builder.CreatePHI(llvmValueTy(), 3, "plus.res");
    phi->addIncoming(numResult, isNumAddBB);
    phi->addIncoming(finalStrResult, isStrConcatBB);
    phi->addIncoming(errorResult, errorBB);
    value = phi;
    return;
  }

  // For other arithmetic and ordering operators, both operands must be numbers
  llvm::BasicBlock *bothNumBB;
  llvm::BasicBlock *errorBB;
  checkBothNumbers(L, R, bothNumBB, errorBB);

  // Fast path: both are numbers
  builder.SetInsertPoint(bothNumBB);
  llvm::Value *Ld = toDouble(L);
  llvm::Value *Rd = toDouble(R);
  llvm::Value *res = nullptr;

  switch (e->op.getType()) {
  case TokenType::MINUS:
    res = fromDouble(builder.CreateFSub(Ld, Rd, "sub"));
    break;
  case TokenType::STAR:
    res = fromDouble(builder.CreateFMul(Ld, Rd, "mul"));
    break;
  case TokenType::SLASH: {
    // Use safe division that checks for division by zero
    auto safeDivFn = mod.getFunction("elx_safe_divide");
    if (safeDivFn) {
      res = builder.CreateCall(safeDivFn, {L, R}, "safe_div");
      // Check for division by zero error after safe divide
      llvm::Function *hasErrorFn = mod.getFunction("elx_has_runtime_error");
      if (hasErrorFn) {
        auto hasError = builder.CreateCall(hasErrorFn, {}, "has_error");
        auto hasErrorBool = builder.CreateICmpNE(
            hasError, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0),
            "error_check");

        // If there's an error, use nil instead of the result
        res = builder.CreateSelect(hasErrorBool, nilConst(), res,
                                   "safe_div_result");
      }
    } else {
      res = fromDouble(builder.CreateFDiv(Ld, Rd, "div"));
    }
    break;
  }

  // Use ordered comparisons to handle NaN properly
  // In Lox, all comparisons with NaN should return false
  case TokenType::GREATER:
    res = makeBool(builder.CreateFCmpOGT(Ld, Rd, "gt"));
    break;
  case TokenType::GREATER_EQUAL:
    res = makeBool(builder.CreateFCmpOGE(Ld, Rd, "ge"));
    break;
  case TokenType::LESS:
    res = makeBool(builder.CreateFCmpOLT(Ld, Rd, "lt"));
    break;
  case TokenType::LESS_EQUAL:
    res = makeBool(builder.CreateFCmpOLE(Ld, Rd, "le"));
    break;

  default:
    res = nilConst(); // shouldn't happen
    break;
  }

  auto contBB = llvm::BasicBlock::Create(ctx, "binop.cont",
                                         builder.GetInsertBlock()->getParent());
  builder.CreateBr(contBB);

  // Handle error case
  builder.SetInsertPoint(errorBB);
  auto errorResult = nilConst();
  builder.CreateBr(contBB);

  // Merge results
  builder.SetInsertPoint(contBB);
  auto phi = builder.CreatePHI(llvmValueTy(), 2, "binop.res");
  phi->addIncoming(res, bothNumBB);
  phi->addIncoming(errorResult, errorBB);
  value = phi;
}

void CodeGenVisitor::visitGroupingExpr(Grouping *e) {
  e->expression->accept(this);
}

void CodeGenVisitor::visitLiteralExpr(Literal *e) {
  if (std::holds_alternative<double>(e->value)) {
    double d = std::get<double>(e->value);
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    recordConstant();
    value = llvm::ConstantInt::get(llvmValueTy(), bits);
  } else if (std::holds_alternative<std::string>(e->value)) {
    // Create a string object
    const std::string &str = std::get<std::string>(e->value);
    value = stringConst(str, true);
  } else if (std::holds_alternative<bool>(e->value)) {
    value = boolConst(std::get<bool>(e->value));
  } else {
    value = nilConst();
  }
}

void CodeGenVisitor::visitUnaryExpr(Unary *e) {
  e->right->accept(this);
  llvm::Value *R = value;

  switch (e->op.getType()) {
  case TokenType::MINUS: {
    auto fn = builder.GetInsertBlock()->getParent();
    auto isNumBB = llvm::BasicBlock::Create(ctx, "neg.num", fn);
    auto slowBB = llvm::BasicBlock::Create(ctx, "neg.slow", fn);
    auto contBB = llvm::BasicBlock::Create(ctx, "neg.cont", fn);

    auto isNum = isNumber(R);
    builder.CreateCondBr(isNum, isNumBB, slowBB);

    builder.SetInsertPoint(isNumBB);
    auto d = toDouble(R);
    auto nd = builder.CreateFNeg(d, "neg");
    auto rv = fromDouble(nd);
    builder.CreateBr(contBB);

    builder.SetInsertPoint(slowBB);
    emitRuntimeError("Operand must be a number for negation.");
    auto errorResult = nilConst();
    builder.CreateBr(contBB);

    builder.SetInsertPoint(contBB);
    auto phi = builder.CreatePHI(llvmValueTy(), 2, "neg.res");
    phi->addIncoming(rv, isNumBB);
    phi->addIncoming(errorResult, slowBB);
    value = phi;
    break;
  }
  case TokenType::BANG: {
    auto falsy = isFalsy(R);
    value = makeBool(falsy); // !x is true when x is falsy
    break;
  }
  default:
    value = R; // shouldn't happen
    break;
  }
}

void CodeGenVisitor::visitVariableExpr(Variable *e) {
  const std::string &varName = e->name.getLexeme();
  bool captured = false;

  // Favour lexical bindings that are currently in scope before consulting any
  // captured upvalues so that shadowing behaves like the reference
  auto stackIt = variableStacks.find(varName);
  if (stackIt != variableStacks.end() && !stackIt->second.empty()) {
    // Get the storage that is current in this lexical scope (top of stack)
    llvm::Value *current_storage = stackIt->second.back();

    if (directValues.count(varName)) {
      value = current_storage; // Direct value, no load needed
    } else {
      value =
          builder.CreateLoad(llvmValueTy(), current_storage, varName.c_str());
    }
    return;
  }

  // For global variables, check the persistent global system AFTER local stacks
  if (globalVariables.count(varName)) {
    auto getGlobalVarFn = mod.getFunction("elx_get_global_variable");
    if (getGlobalVarFn) {
      auto nameStr = builder.CreateGlobalStringPtr(varName, "var_name");
      value = builder.CreateCall(getGlobalVarFn, {nameStr}, "global_var");
      return;
    }
    return;
  }

  // Fall back to "_current" binding for variables not using the stack system
  auto currentIt = locals.find(varName + "_current");
  if (!captured && currentIt != locals.end()) {
    // Check if this is a direct value (like a parameter) or needs to be loaded
    if (directValues.count(varName)) {
      value = currentIt->second; // Direct value, no load needed
    } else {
      value =
          builder.CreateLoad(llvmValueTy(), currentIt->second, varName.c_str());
    }
    return;
  }

  // Finally resolve captured variables (upvalues) once no lexical binding is
  // active. This allows locals declared inside loops to shadow closed-over
  // variables, matching Crafting Interpreters semantics.
  if (!function_stack.empty()) {
    const FunctionContext &current_ctx = function_stack.top();
    auto upvalue_it = current_ctx.upvalue_indices.find(varName);
    if (upvalue_it != current_ctx.upvalue_indices.end()) {
      int upvalue_index = upvalue_it->second;
      value = accessUpvalue(varName, upvalue_index);
      return;
    }
  }

  // Fallback to original lookup for variables declared before this system
  auto it = locals.find(varName);
  if (!captured && it != locals.end()) {
    // Check if this is a direct value (like a parameter) or needs to be loaded
    if (directValues.count(varName)) {
      value = it->second; // Direct value, no load needed
    } else {
      value = builder.CreateLoad(llvmValueTy(), it->second, varName.c_str());
    }
    return;
  }

  // If no lexical binding exists, captured upvalues are the next choice.
  if (!function_stack.empty()) {
    const FunctionContext &current_ctx = function_stack.top();
    auto upvalue_it = current_ctx.upvalue_indices.find(varName);
    if (upvalue_it != current_ctx.upvalue_indices.end()) {
      int upvalue_index = upvalue_it->second;
      value = accessUpvalue(varName, upvalue_index);
      return;
    }
  }

  // For global variables, check the persistent global system after locals.
  if (globalVariables.count(varName)) {
    auto getGlobalVarFn = mod.getFunction("elx_get_global_variable");
    if (getGlobalVarFn) {
      auto nameStr = builder.CreateGlobalStringPtr(varName, "var_name");
      value = builder.CreateCall(getGlobalVarFn, {nameStr}, "global_var");
      return;
    }
  }

  // Check globals (current module scope)
  auto globalIt = globals.find(varName);
  if (globalIt != globals.end()) {
    value = globalIt->second;
    return;
  }

  // Check if this is a declared function that hasn't been fully processed yet
  auto funcIt = functions.find(varName);
  if (funcIt != functions.end()) {
    // For forward-declared functions, we need to look them up at runtime
    // This is because function objects can't be created at compile time without
    // causing cross-function reference issues in LLVM IR

    // Check persistent global functions first
    auto hasGlobalFuncFn = mod.getFunction("elx_has_global_function");
    auto getGlobalFuncFn = mod.getFunction("elx_get_global_function");
    if (hasGlobalFuncFn && getGlobalFuncFn) {
      auto nameStr = builder.CreateGlobalStringPtr(varName, "func_name");
      auto hasFunc =
          builder.CreateCall(hasGlobalFuncFn, {nameStr}, "has_global_func");
      auto zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0);
      auto hasFuncBool = builder.CreateICmpNE(hasFunc, zero, "has_func_bool");

      auto fn = builder.GetInsertBlock()->getParent();
      auto foundFuncBB = llvm::BasicBlock::Create(ctx, "found_func", fn);
      auto notFoundBB = llvm::BasicBlock::Create(ctx, "not_found", fn);
      auto contBB = llvm::BasicBlock::Create(ctx, "cont", fn);

      builder.CreateCondBr(hasFuncBool, foundFuncBB, notFoundBB);

      // Found global function
      builder.SetInsertPoint(foundFuncBB);
      auto funcValue =
          builder.CreateCall(getGlobalFuncFn, {nameStr}, "global_func");
      builder.CreateBr(contBB);

      // Function not found - runtime error
      builder.SetInsertPoint(notFoundBB);
      emitRuntimeError("Undefined function '" + varName + "'.");
      auto notFoundValue = nilConst();
      builder.CreateBr(contBB);

      // Merge paths
      builder.SetInsertPoint(contBB);
      auto phi = builder.CreatePHI(llvmValueTy(), 2, "func_result");
      phi->addIncoming(funcValue, foundFuncBB);
      phi->addIncoming(notFoundValue, notFoundBB);
      value = phi;
      return;
    }

    // Fallback error
    std::cerr << "Error: Function '" << varName
              << "' declared but runtime lookup unavailable\n";
    value = nilConst();
    return;
  }

  // Check persistent global variables
  auto hasGlobalVarFn = mod.getFunction("elx_has_global_variable");
  auto getGlobalVarFn = mod.getFunction("elx_get_global_variable");
  if (hasGlobalVarFn && getGlobalVarFn) {
    auto nameStr = builder.CreateGlobalStringPtr(varName, "var_name");
    auto hasVar =
        builder.CreateCall(hasGlobalVarFn, {nameStr}, "has_global_var");
    auto zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0);
    auto hasVarBool = builder.CreateICmpNE(hasVar, zero, "has_var_bool");

    auto fn = builder.GetInsertBlock()->getParent();
    auto foundVarBB = llvm::BasicBlock::Create(ctx, "found_var", fn);
    auto checkFuncBB = llvm::BasicBlock::Create(ctx, "check_func", fn);
    auto contBB = llvm::BasicBlock::Create(ctx, "cont", fn);

    builder.CreateCondBr(hasVarBool, foundVarBB, checkFuncBB);

    // Found global variable
    builder.SetInsertPoint(foundVarBB);
    auto varValue = builder.CreateCall(getGlobalVarFn, {nameStr}, "global_var");
    builder.CreateBr(contBB);

    // Check global functions
    builder.SetInsertPoint(checkFuncBB);
    auto hasGlobalFuncFn = mod.getFunction("elx_has_global_function");
    auto getGlobalFuncFn = mod.getFunction("elx_get_global_function");
    if (hasGlobalFuncFn && getGlobalFuncFn) {
      auto hasFunc =
          builder.CreateCall(hasGlobalFuncFn, {nameStr}, "has_global_func");
      auto hasFuncBool = builder.CreateICmpNE(hasFunc, zero, "has_func_bool");

      auto foundFuncBB = llvm::BasicBlock::Create(ctx, "found_func", fn);
      auto checkBuiltinBB = llvm::BasicBlock::Create(ctx, "check_builtin", fn);

      builder.CreateCondBr(hasFuncBool, foundFuncBB, checkBuiltinBB);

      // Found global function
      builder.SetInsertPoint(foundFuncBB);
      auto funcValue =
          builder.CreateCall(getGlobalFuncFn, {nameStr}, "global_func");
      builder.CreateBr(contBB);

      // Check builtins
      builder.SetInsertPoint(checkBuiltinBB);
      auto getBuiltinFn = mod.getFunction("elx_get_global_builtin");
      if (getBuiltinFn) {
        auto builtinValue =
            builder.CreateCall(getBuiltinFn, {nameStr}, "builtin_check");
        auto nilValue = nilConst();
        auto isNotNil =
            builder.CreateICmpNE(builtinValue, nilValue, "is_builtin");

        auto foundBuiltinBB =
            llvm::BasicBlock::Create(ctx, "found_builtin", fn);
        auto notFoundBB = llvm::BasicBlock::Create(ctx, "not_found", fn);

        builder.CreateCondBr(isNotNil, foundBuiltinBB, notFoundBB);

        // Found builtin
        builder.SetInsertPoint(foundBuiltinBB);
        builder.CreateBr(contBB);

        // Variable not found - runtime error
        builder.SetInsertPoint(notFoundBB);
        emitRuntimeError("Undefined variable '" + varName + "'.");
        auto notFoundValue = nilConst();
        builder.CreateBr(contBB);

        // Merge all paths
        builder.SetInsertPoint(contBB);
        auto phi = builder.CreatePHI(llvmValueTy(), 5, "var_result");
        phi->addIncoming(varValue, foundVarBB);
        phi->addIncoming(funcValue, foundFuncBB);
        phi->addIncoming(builtinValue, foundBuiltinBB);
        phi->addIncoming(notFoundValue, notFoundBB);
        value = phi;
        return;
      }
    }
  }

  // Fallback - return nil and print error
  emitRuntimeError("Undefined variable '" + varName + "'.");
  value = nilConst();
}

void CodeGenVisitor::visitAssignExpr(Assign *e) {
  e->value->accept(this);
  llvm::Value *assignValue = value;

  const std::string &varName = e->name.getLexeme();

  // Use variable stacks for lexical scope correctness in assignments first so
  // locals shadow captured values.
  auto stackIt = variableStacks.find(varName);
  if (stackIt != variableStacks.end() && !stackIt->second.empty()) {
    // Get the storage that is current in this lexical scope (top of stack)
    llvm::Value *current_storage = stackIt->second.back();

    if (directValues.count(varName)) {
      // This shouldn't happen for variable stack entries, but handle it by
      // materialising storage.
      auto fn = builder.GetInsertBlock()->getParent();
      auto slot = createStackAlloca(fn, llvmValueTy(), varName);
      builder.CreateStore(current_storage, slot);
      stackIt->second.back() = slot; // Update the stack entry
      directValues.erase(varName);
      current_storage = slot;
    }

    builder.CreateStore(assignValue, current_storage);
    value = assignValue; // Assignment returns the assigned value
    return;
  }

  // For global variables AFTER checking local stacks
  if (globalVariables.count(varName)) {
    auto localIt = locals.find(varName);
    if (localIt != locals.end()) {
      if (directValues.count(varName)) {
        auto fn = builder.GetInsertBlock()->getParent();
        auto slot = createStackAlloca(fn, llvmValueTy(), varName);
        locals[varName] = slot;
        directValues.erase(varName);
        localIt->second = slot;
      }
      builder.CreateStore(assignValue, localIt->second);
    }

    auto setGlobalVarFn = mod.getFunction("elx_set_global_variable");
    if (setGlobalVarFn) {
      auto nameStr = builder.CreateGlobalStringPtr(varName, "var_name");
      builder.CreateCall(setGlobalVarFn, {nameStr, assignValue});
    }

    value = assignValue; // Assignment returns the assigned value
    return;
  }

  // Update captured variables only when no lexical binding handled the
  // assignment.
  if (!function_stack.empty()) {
    const FunctionContext &current_ctx = function_stack.top();
    auto upvalue_it = current_ctx.upvalue_indices.find(varName);
    if (upvalue_it != current_ctx.upvalue_indices.end()) {
      int upvalue_index = upvalue_it->second;

      auto idxVal =
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), upvalue_index);
      auto ptr =
          builder.CreateGEP(llvmValueTy(), current_ctx.upvalue_array, idxVal);
      auto upvalue_bits = builder.CreateLoad(llvmValueTy(), ptr);
      auto set_upvalue_fn = mod.getFunction("elx_set_upvalue_value");
      if (set_upvalue_fn) {
        builder.CreateCall(set_upvalue_fn, {upvalue_bits, assignValue});
      }
      value = assignValue;
      return;
    }
  }

  // Check locals for truly local variables - use "_current" binding
  auto currentIt = locals.find(varName + "_current");
  if (currentIt != locals.end()) {
    if (directValues.count(varName)) {
      // This is a parameter or direct value - we need to create storage for it
      auto fn = builder.GetInsertBlock()->getParent();
      auto slot = createStackAlloca(fn, llvmValueTy(), varName);
      locals[varName + "_current"] = slot;
      directValues.erase(varName);
    }
    builder.CreateStore(assignValue, currentIt->second);
    value = assignValue; // Assignment returns the assigned value
    return;
  }

  // Fallback to original lookup
  auto localIt = locals.find(varName);
  if (localIt != locals.end()) {
    if (directValues.count(varName)) {
      // This is a parameter or direct value - we need to create storage for it
      auto fn = builder.GetInsertBlock()->getParent();
      auto slot = createStackAlloca(fn, llvmValueTy(), varName);
      locals[varName] = slot;
      directValues.erase(varName);
    }
    builder.CreateStore(assignValue, localIt->second);
    value = assignValue; // Assignment returns the assigned value
    return;
  }

  auto globalIt = globals.find(varName);
  if (globalIt != globals.end()) {
    globals[varName] = assignValue;
    value = assignValue;
    return;
  }

  auto hasGlobalVarFn = mod.getFunction("elx_has_global_variable");
  auto setGlobalVarFn = mod.getFunction("elx_set_global_variable");
  if (hasGlobalVarFn && setGlobalVarFn) {
    auto nameStr = builder.CreateGlobalStringPtr(varName, "var_name");
    auto hasVar =
        builder.CreateCall(hasGlobalVarFn, {nameStr}, "has_global_var");
    auto hasVarBool =
        builder.CreateICmpNE(hasVar, builder.getInt32(0), "has_var_bool");

    auto fn = builder.GetInsertBlock()->getParent();
    auto assignGlobalBB = llvm::BasicBlock::Create(ctx, "assign_global", fn);
    auto errorBB = llvm::BasicBlock::Create(ctx, "assign_error", fn);
    auto contBB = llvm::BasicBlock::Create(ctx, "assign_cont", fn);

    builder.CreateCondBr(hasVarBool, assignGlobalBB, errorBB);

    builder.SetInsertPoint(assignGlobalBB);
    builder.CreateCall(setGlobalVarFn, {nameStr, assignValue});
    builder.CreateBr(contBB);

    builder.SetInsertPoint(errorBB);
    emitRuntimeError("Undefined variable '" + varName + "'.");
    builder.CreateBr(contBB);

    builder.SetInsertPoint(contBB);
    auto phi = builder.CreatePHI(llvmValueTy(), 2, "assign_result");
    phi->addIncoming(assignValue, assignGlobalBB);
    phi->addIncoming(nilConst(), errorBB);
    value = phi;
    return;
  }

  // Variable not found - this is an error in Lox
  emitRuntimeError("Undefined variable '" + varName + "'.");
  value = nilConst();
}

void CodeGenVisitor::visitLogicalExpr(Logical *e) {
  // Evaluate left
  e->left->accept(this);
  llvm::Value *L = value;

  auto fn = builder.GetInsertBlock()->getParent();
  auto leftBB = builder.GetInsertBlock();
  auto rightBB = llvm::BasicBlock::Create(ctx, "logic.right", fn);
  auto endBB = llvm::BasicBlock::Create(ctx, "logic.end", fn);

  llvm::Value *leftTruthy = isTruthy(L);

  if (e->op.getType() == TokenType::OR) {
    // if left is truthy -> skip right
    builder.CreateCondBr(leftTruthy, endBB, rightBB);
  } else { // AND
    // if left is truthy -> evaluate right, else skip
    builder.CreateCondBr(leftTruthy, rightBB, endBB);
  }

  // Right side
  builder.SetInsertPoint(rightBB);
  e->right->accept(this);
  llvm::Value *R = value;
  builder.CreateBr(endBB);
  auto rightEvalBB = builder.GetInsertBlock();

  // Merge
  builder.SetInsertPoint(endBB);
  auto phi = builder.CreatePHI(llvmValueTy(), 2, "logic.res");
  if (e->op.getType() == TokenType::OR) {
    phi->addIncoming(L, leftBB);
    phi->addIncoming(R, rightEvalBB);
  } else { // AND
    phi->addIncoming(R, rightEvalBB);
    phi->addIncoming(L, leftBB);
  }
  value = phi;
}

void CodeGenVisitor::visitCallExpr(Call *e) {
  if (e->arguments.size() > static_cast<size_t>(MAX_PARAMETERS)) {
    throw CompileError("Can't have more than 255 arguments.");
  }

  e->callee->accept(this);
  llvm::Value *callee = value;

  std::vector<llvm::Value *> args;
  for (auto &arg : e->arguments) {
    arg->accept(this);
    args.push_back(value);
  }

  llvm::Function *callValueFn = mod.getFunction("elx_call_value");
  llvm::Function *callFunctionFastFn = mod.getFunction("elx_call_function_fast");
  llvm::Function *callClosureFastFn = mod.getFunction("elx_call_closure_fast");
  llvm::Function *callNativeFastFn = mod.getFunction("elx_call_native_fast");
  llvm::Function *callBoundFastFn =
      mod.getFunction("elx_call_bound_method_fast");
  llvm::Function *callClassFastFn = mod.getFunction("elx_call_class_fast");
  llvm::Function *callCacheUpdateFn = mod.getFunction("elx_call_cache_update");
  llvm::Function *isFunctionFn = mod.getFunction("elx_is_function");
  llvm::Function *isClosureFn = mod.getFunction("elx_is_closure");
  llvm::Function *isNativeFn = mod.getFunction("elx_is_native");
  llvm::Function *isClassFn = mod.getFunction("elx_is_class");
  llvm::Function *isBoundMethodFn = mod.getFunction("elx_is_bound_method");
  llvm::Function *boundMatchesFn =
      mod.getFunction("elx_bound_method_matches");

  llvm::Value *argArray = nullptr;
  llvm::Value *argCount =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), args.size());

  if (!args.empty()) {
    argArray = builder.CreateAlloca(
        llvmValueTy(),
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), args.size()),
        "args");

    for (size_t i = 0; i < args.size(); ++i) {
      llvm::Value *idx =
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), i);
      llvm::Value *elemPtr = builder.CreateGEP(llvmValueTy(), argArray, idx);
      builder.CreateStore(args[i], elemPtr);
    }
  } else {
    argArray = llvm::ConstantPointerNull::get(
        llvm::PointerType::get(llvmValueTy(), 0));
  }

  if (!callValueFn || !callFunctionFastFn || !callClosureFastFn ||
      !callNativeFastFn || !callBoundFastFn || !callClassFastFn ||
      !callCacheUpdateFn || !isFunctionFn || !isClosureFn || !isNativeFn ||
      !isClassFn || !isBoundMethodFn || !boundMatchesFn) {
    if (!callValueFn) {
      value = nilConst();
      return;
    }
    value = builder.CreateCall(callValueFn, {callee, argArray, argCount});
    checkRuntimeError(value);
    return;
  }

  auto cacheGV = getCallCacheGlobal("call", e);
  auto cacheTy = getCallCacheType();
  auto int32Ty = llvm::Type::getInt32Ty(ctx);
  auto int64Ty = llvm::Type::getInt64Ty(ctx);
  auto i8PtrTy = llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0);
  auto zero32 = llvm::ConstantInt::get(int32Ty, 0);
  auto zero64 = llvm::ConstantInt::get(int64Ty, 0);
  auto nullI8Ptr = llvm::ConstantPointerNull::get(i8PtrTy);

  auto kindPtr = builder.CreateStructGEP(cacheTy, cacheGV, 5,
                                         "call_cache_kind_ptr");
  auto kindVal = builder.CreateLoad(int32Ty, kindPtr, "call_cache_kind");

  llvm::Function *fn = builder.GetInsertBlock()->getParent();

  auto functionCheckBB =
      llvm::BasicBlock::Create(ctx, "call.cache.function", fn);
  auto closureCheckBB =
      llvm::BasicBlock::Create(ctx, "call.cache.closure", fn);
  auto nativeCheckBB =
      llvm::BasicBlock::Create(ctx, "call.cache.native", fn);
  auto boundCheckBB =
      llvm::BasicBlock::Create(ctx, "call.cache.bound", fn);
  auto classCheckBB =
      llvm::BasicBlock::Create(ctx, "call.cache.class", fn);
  auto slowBB = llvm::BasicBlock::Create(ctx, "call.cache.slow", fn);
  auto exitBB = llvm::BasicBlock::Create(ctx, "call.cache.exit", fn);

  builder.CreateBr(functionCheckBB);

  std::vector<std::pair<llvm::BasicBlock *, llvm::Value *>> results;

  builder.SetInsertPoint(functionCheckBB);
  auto functionKindConst = llvm::ConstantInt::get(
      int32Ty, static_cast<int>(eloxir::CallInlineCacheKind::FUNCTION));
  auto isFunctionKind =
      builder.CreateICmpEQ(kindVal, functionKindConst, "cache_function_kind");
  auto functionGuardBB =
      llvm::BasicBlock::Create(ctx, "call.cache.function.guard", fn);
  builder.CreateCondBr(isFunctionKind, functionGuardBB, closureCheckBB);

  builder.SetInsertPoint(functionGuardBB);
  auto isFunctionValue =
      builder.CreateCall(isFunctionFn, {callee}, "is_function");
  auto isFunctionBool =
      builder.CreateICmpNE(isFunctionValue, zero32, "is_function_bool");
  auto functionCalleePtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 0, "function_callee_ptr");
  auto cachedFunction =
      builder.CreateLoad(llvmValueTy(), functionCalleePtr, "cached_function");
  auto functionMatch =
      builder.CreateICmpEQ(callee, cachedFunction, "function_match");
  auto functionTargetPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 3, "function_target_ptr");
  auto functionTarget =
      builder.CreateLoad(i8PtrTy, functionTargetPtr, "function_target");
  auto functionTargetValid =
      builder.CreateICmpNE(functionTarget, nullI8Ptr, "function_target_valid");
  auto functionExpectedPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 4, "function_expected_ptr");
  auto functionExpected = builder.CreateLoad(int32Ty, functionExpectedPtr,
                                             "function_expected");
  auto functionMeta =
      builder.CreateAnd(isFunctionBool, functionMatch, "function_meta");
  auto functionReady =
      builder.CreateAnd(functionMeta, functionTargetValid, "function_ready");
  auto functionFastBB =
      llvm::BasicBlock::Create(ctx, "call.cache.function.fast", fn);
  builder.CreateCondBr(functionReady, functionFastBB, closureCheckBB);

  builder.SetInsertPoint(functionFastBB);
  auto functionResult = builder.CreateCall(
      callFunctionFastFn,
      {callee, argArray, argCount, functionTarget, functionExpected},
      "call_function_fast");
#ifdef ELOXIR_ENABLE_CACHE_STATS
  if (auto *callHitFn = mod.getFunction("elx_cache_stats_record_call_hit")) {
    builder.CreateCall(callHitFn, {functionKindConst});
  }
#endif
  builder.CreateBr(exitBB);
  results.emplace_back(builder.GetInsertBlock(), functionResult);

  builder.SetInsertPoint(closureCheckBB);
  auto closureKindConst = llvm::ConstantInt::get(
      int32Ty, static_cast<int>(eloxir::CallInlineCacheKind::CLOSURE));
  auto isClosureKind =
      builder.CreateICmpEQ(kindVal, closureKindConst, "cache_closure_kind");
  auto closureGuardBB =
      llvm::BasicBlock::Create(ctx, "call.cache.closure.guard", fn);
  builder.CreateCondBr(isClosureKind, closureGuardBB, nativeCheckBB);

  builder.SetInsertPoint(closureGuardBB);
  auto isClosureValue =
      builder.CreateCall(isClosureFn, {callee}, "is_closure");
  auto isClosureBool =
      builder.CreateICmpNE(isClosureValue, zero32, "is_closure_bool");
  auto closureCalleePtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 0, "closure_callee_ptr");
  auto cachedClosure =
      builder.CreateLoad(llvmValueTy(), closureCalleePtr, "cached_closure");
  auto closureMatch =
      builder.CreateICmpEQ(callee, cachedClosure, "closure_match");
  auto closureTargetPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 3, "closure_target_ptr");
  auto closureTarget =
      builder.CreateLoad(i8PtrTy, closureTargetPtr, "closure_target");
  auto closureTargetValid = builder.CreateICmpNE(closureTarget, nullI8Ptr,
                                                 "closure_target_valid");
  auto closureExpectedPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 4, "closure_expected_ptr");
  auto closureExpected = builder.CreateLoad(int32Ty, closureExpectedPtr,
                                            "closure_expected");
  auto closureMeta =
      builder.CreateAnd(isClosureBool, closureMatch, "closure_meta");
  auto closureReady =
      builder.CreateAnd(closureMeta, closureTargetValid, "closure_ready");
  auto closureFastBB =
      llvm::BasicBlock::Create(ctx, "call.cache.closure.fast", fn);
  builder.CreateCondBr(closureReady, closureFastBB, nativeCheckBB);

  builder.SetInsertPoint(closureFastBB);
  auto closureResult = builder.CreateCall(
      callClosureFastFn,
      {callee, argArray, argCount, closureTarget, closureExpected},
      "call_closure_fast");
#ifdef ELOXIR_ENABLE_CACHE_STATS
  if (auto *callHitFn = mod.getFunction("elx_cache_stats_record_call_hit")) {
    builder.CreateCall(callHitFn, {closureKindConst});
  }
#endif
  builder.CreateBr(exitBB);
  results.emplace_back(builder.GetInsertBlock(), closureResult);

  builder.SetInsertPoint(nativeCheckBB);
  auto nativeKindConst = llvm::ConstantInt::get(
      int32Ty, static_cast<int>(eloxir::CallInlineCacheKind::NATIVE));
  auto isNativeKind =
      builder.CreateICmpEQ(kindVal, nativeKindConst, "cache_native_kind");
  auto nativeGuardBB =
      llvm::BasicBlock::Create(ctx, "call.cache.native.guard", fn);
  builder.CreateCondBr(isNativeKind, nativeGuardBB, boundCheckBB);

  builder.SetInsertPoint(nativeGuardBB);
  auto isNativeValue =
      builder.CreateCall(isNativeFn, {callee}, "is_native");
  auto isNativeBool =
      builder.CreateICmpNE(isNativeValue, zero32, "is_native_bool");
  auto nativeCalleePtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 0, "native_callee_ptr");
  auto cachedNative =
      builder.CreateLoad(llvmValueTy(), nativeCalleePtr, "cached_native");
  auto nativeMatch =
      builder.CreateICmpEQ(callee, cachedNative, "native_match");
  auto nativeTargetPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 3, "native_target_ptr");
  auto nativeTarget =
      builder.CreateLoad(i8PtrTy, nativeTargetPtr, "native_target");
  auto nativeTargetValid = builder.CreateICmpNE(nativeTarget, nullI8Ptr,
                                                "native_target_valid");
  auto nativeExpectedPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 4, "native_expected_ptr");
  auto nativeExpected = builder.CreateLoad(int32Ty, nativeExpectedPtr,
                                           "native_expected");
  auto nativeMeta =
      builder.CreateAnd(isNativeBool, nativeMatch, "native_meta");
  auto nativeReady =
      builder.CreateAnd(nativeMeta, nativeTargetValid, "native_ready");
  auto nativeFastBB =
      llvm::BasicBlock::Create(ctx, "call.cache.native.fast", fn);
  builder.CreateCondBr(nativeReady, nativeFastBB, boundCheckBB);

  builder.SetInsertPoint(nativeFastBB);
  auto nativeResult = builder.CreateCall(
      callNativeFastFn,
      {callee, argArray, argCount, nativeTarget, nativeExpected},
      "call_native_fast");
#ifdef ELOXIR_ENABLE_CACHE_STATS
  if (auto *callHitFn = mod.getFunction("elx_cache_stats_record_call_hit")) {
    builder.CreateCall(callHitFn, {nativeKindConst});
  }
#endif
  builder.CreateBr(exitBB);
  results.emplace_back(builder.GetInsertBlock(), nativeResult);

  builder.SetInsertPoint(boundCheckBB);
  auto boundKindConst = llvm::ConstantInt::get(
      int32Ty, static_cast<int>(eloxir::CallInlineCacheKind::BOUND_METHOD));
  auto isBoundKind =
      builder.CreateICmpEQ(kindVal, boundKindConst, "cache_bound_kind");
  auto boundGuardBB =
      llvm::BasicBlock::Create(ctx, "call.cache.bound.guard", fn);
  builder.CreateCondBr(isBoundKind, boundGuardBB, classCheckBB);

  builder.SetInsertPoint(boundGuardBB);
  auto isBoundValue =
      builder.CreateCall(isBoundMethodFn, {callee}, "is_bound_method");
  auto isBoundBool =
      builder.CreateICmpNE(isBoundValue, zero32, "is_bound_bool");
  auto boundMethodPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 1, "bound_method_ptr");
  auto cachedMethod =
      builder.CreateLoad(llvmValueTy(), boundMethodPtr, "cached_method");
  auto hasMethodBits =
      builder.CreateICmpNE(cachedMethod, zero64, "bound_has_method");
  auto boundClassPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 2, "bound_class_ptr");
  auto cachedClass =
      builder.CreateLoad(llvmValueTy(), boundClassPtr, "cached_class");
  auto boundTargetPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 3, "bound_target_ptr");
  auto boundTarget =
      builder.CreateLoad(i8PtrTy, boundTargetPtr, "bound_target");
  auto boundTargetValid = builder.CreateICmpNE(boundTarget, nullI8Ptr,
                                               "bound_target_valid");
  auto boundExpectedPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 4, "bound_expected_ptr");
  auto boundExpected = builder.CreateLoad(int32Ty, boundExpectedPtr,
                                          "bound_expected");
  auto boundFlagsPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 6, "bound_flags_ptr");
  auto boundFlags =
      builder.CreateLoad(int32Ty, boundFlagsPtr, "bound_flags");
  auto hasFlags =
      builder.CreateICmpNE(boundFlags, zero32, "bound_has_flags");
  auto boundMatchesCall = builder.CreateCall(
      boundMatchesFn, {callee, cachedMethod, cachedClass}, "bound_matches");
  auto boundMatchesBool =
      builder.CreateICmpNE(boundMatchesCall, zero32, "bound_matches_bool");
  llvm::Value *boundCond =
      builder.CreateAnd(isBoundBool, hasMethodBits, "bound_meta");
  boundCond = builder.CreateAnd(boundCond, boundMatchesBool, "bound_match");
  boundCond = builder.CreateAnd(boundCond, boundTargetValid,
                                "bound_target_ready");
  boundCond = builder.CreateAnd(boundCond, hasFlags, "bound_ready");
  auto boundFastBB =
      llvm::BasicBlock::Create(ctx, "call.cache.bound.fast", fn);
  builder.CreateCondBr(boundCond, boundFastBB, classCheckBB);

  builder.SetInsertPoint(boundFastBB);
  auto boundResult = builder.CreateCall(
      callBoundFastFn,
      {callee, argArray, argCount, cachedMethod, boundTarget, boundExpected,
       cachedClass, boundFlags},
      "call_bound_fast");
#ifdef ELOXIR_ENABLE_CACHE_STATS
  if (auto *callHitFn = mod.getFunction("elx_cache_stats_record_call_hit")) {
    builder.CreateCall(callHitFn, {boundKindConst});
  }
#endif
  builder.CreateBr(exitBB);
  results.emplace_back(builder.GetInsertBlock(), boundResult);

  builder.SetInsertPoint(classCheckBB);
  auto classKindConst = llvm::ConstantInt::get(
      int32Ty, static_cast<int>(eloxir::CallInlineCacheKind::CLASS));
  auto isClassKind =
      builder.CreateICmpEQ(kindVal, classKindConst, "cache_class_kind");
  auto classGuardBB =
      llvm::BasicBlock::Create(ctx, "call.cache.class.guard", fn);
  builder.CreateCondBr(isClassKind, classGuardBB, slowBB);

  builder.SetInsertPoint(classGuardBB);
  auto isClassValue = builder.CreateCall(isClassFn, {callee}, "is_class");
  auto isClassBool =
      builder.CreateICmpNE(isClassValue, zero32, "is_class_bool");
  auto classCalleePtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 0, "class_callee_ptr");
  auto cachedClassCallee = builder.CreateLoad(llvmValueTy(), classCalleePtr,
                                              "cached_class_bits");
  auto classMatch =
      builder.CreateICmpEQ(callee, cachedClassCallee, "class_match");
  auto classMethodPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 1, "class_method_ptr");
  auto classMethodBits =
      builder.CreateLoad(llvmValueTy(), classMethodPtr, "class_method_bits");
  auto classTargetPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 3, "class_target_ptr");
  auto classTarget =
      builder.CreateLoad(i8PtrTy, classTargetPtr, "class_target");
  auto classExpectedPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 4, "class_expected_ptr");
  auto classExpected = builder.CreateLoad(int32Ty, classExpectedPtr,
                                          "class_expected");
  auto classFlagsPtr =
      builder.CreateStructGEP(cacheTy, cacheGV, 6, "class_flags_ptr");
  auto classFlags = builder.CreateLoad(int32Ty, classFlagsPtr,
                                       "class_flags");
  auto initMask = builder.CreateAnd(
      classFlags,
      llvm::ConstantInt::get(int32Ty,
                             static_cast<int>(eloxir::CALL_CACHE_FLAG_CLASS_HAS_INITIALIZER)),
      "class_init_mask");
  auto hasInitializer =
      builder.CreateICmpNE(initMask, zero32, "class_has_initializer");
  auto methodNonZero =
      builder.CreateICmpNE(classMethodBits, zero64, "class_method_nonzero");
  auto classTargetValid =
      builder.CreateICmpNE(classTarget, nullI8Ptr, "class_target_valid");
  auto initReady =
      builder.CreateAnd(methodNonZero, classTargetValid, "class_init_ready");
  auto noInitializer = builder.CreateNot(hasInitializer, "class_no_init");
  auto initializerReady = builder.CreateOr(noInitializer, initReady,
                                           "class_init_ok");
  auto classMeta =
      builder.CreateAnd(isClassBool, classMatch, "class_meta");
  auto classReady =
      builder.CreateAnd(classMeta, initializerReady, "class_ready");
  auto classFastBB =
      llvm::BasicBlock::Create(ctx, "call.cache.class.fast", fn);
  builder.CreateCondBr(classReady, classFastBB, slowBB);

  builder.SetInsertPoint(classFastBB);
  auto classResult = builder.CreateCall(
      callClassFastFn,
      {callee, argArray, argCount, classMethodBits, classTarget, classExpected,
       classFlags},
      "call_class_fast");
#ifdef ELOXIR_ENABLE_CACHE_STATS
  if (auto *callHitFn = mod.getFunction("elx_cache_stats_record_call_hit")) {
    builder.CreateCall(callHitFn, {classKindConst});
  }
#endif
  builder.CreateBr(exitBB);
  results.emplace_back(builder.GetInsertBlock(), classResult);

  builder.SetInsertPoint(slowBB);
#ifdef ELOXIR_ENABLE_CACHE_STATS
  if (auto *callMissFn = mod.getFunction("elx_cache_stats_record_call_miss")) {
    builder.CreateCall(callMissFn, {});
  }
#endif
  auto slowResult =
      builder.CreateCall(callValueFn, {callee, argArray, argCount},
                         "call_slow");
  builder.CreateCall(callCacheUpdateFn, {cacheGV, callee});
  builder.CreateBr(exitBB);
  results.emplace_back(builder.GetInsertBlock(), slowResult);

  builder.SetInsertPoint(exitBB);
  llvm::Value *resultValue = nullptr;
  if (results.size() == 1) {
    resultValue = results[0].second;
  } else {
    auto phi =
        builder.CreatePHI(llvmValueTy(), results.size(), "call.result");
    for (auto &entry : results) {
      phi->addIncoming(entry.second, entry.first);
    }
    resultValue = phi;
  }

  value = resultValue;
  checkRuntimeError(value);
}

// --------- Stmt visitors -------------------------------------------------
void CodeGenVisitor::visitExpressionStmt(Expression *s) {
  s->expression->accept(this);
  addLoopInstructions(2);
}

void CodeGenVisitor::visitPrintStmt(Print *s) {
  s->expression->accept(this);
  llvm::Function *printFn = mod.getFunction("elx_print");
  builder.CreateCall(printFn, {value});
  addLoopInstructions(2);
}

void CodeGenVisitor::visitVarStmt(Var *s) {
  visitVarStmtWithExecution(s, 1); // Default execution count
}

void CodeGenVisitor::visitVarStmtWithExecution(Var *s, int blockExecution) {
  // Evaluate initializer or use nil
  if (s->initializer) {
    s->initializer->accept(this);
  } else {
    value = nilConst();
  }

  const std::string &varName = s->name.getLexeme();
  llvm::Value *initValue = value;

  // Determine if we're at global scope
  auto fn = builder.GetInsertBlock()->getParent();
  // A declaration is truly global only at top level (no surrounding block).
  bool isGlobal = ((currentFunction == nullptr) ||
                   fn->getName().str().find("__expr") == 0) &&
                  (blockDepth == 0);

  if (isGlobal) {
    // For global variables, create an alloca so they can be modified
    auto fn = builder.GetInsertBlock()->getParent();
    auto slot = createStackAlloca(fn, llvmValueTy(), varName);
    builder.CreateStore(initValue, slot);
    locals[varName] = slot;
    // Don't add to directValues since this needs to be loaded

    // Track this as a global variable
    globalVariables.insert(varName);

    // Also store in persistent global environment for cross-line access
    auto setGlobalVarFn = mod.getFunction("elx_set_global_variable");
    if (setGlobalVarFn) {
      auto nameStr = builder.CreateGlobalStringPtr(varName, "var_name");
      builder.CreateCall(setGlobalVarFn, {nameStr, initValue});
    }
  } else {
    // Local variable - create alloca in function entry block

    if (!function_stack.empty()) {
      FunctionContext &ctx = function_stack.top();
      if (ctx.local_slots.size() >=
          static_cast<size_t>(MAX_USER_LOCAL_SLOTS)) {
        throw CompileError("Too many local variables in function.");
      }
    }

    // DEFINITIVE FIX FOR LOOP VARIABLE CAPTURE BUG:
    // Always create completely unique storage for each variable declaration,
    // even if the same name exists. This prevents storage reuse that causes
    // closure capture bugs.
    std::string allocaName = varName + "_scope" + std::to_string(blockDepth) +
                             "_decl" + std::to_string(variableCounter);
    std::string uniqueVarKey = varName + "#" + std::to_string(blockDepth) +
                               "#" + std::to_string(variableCounter);
    variableCounter++;

    auto slot =
        createStackAlloca(fn, llvmValueTy(), allocaName);
    builder.CreateStore(initValue, slot);

    // Store with the unique key for internal tracking
    locals[uniqueVarKey] = slot;

    // CRITICAL FIX: Don't update "_current" binding when using variable stacks
    // The "_current" system interferes with proper lexical scoping
    // Only update "_current" if this is the first declaration of this variable
    if (variableStacks[varName].empty()) {
      locals[varName + "_current"] = slot;
    }

    // ARCHITECTURAL FIX: Push storage onto per-variable stack for proper
    // lexical scoping
    variableStacks[varName].push_back(slot);

    if (!function_stack.empty()) {
      FunctionContext &ctx = function_stack.top();
      ctx.local_slots.push_back(slot);
      ctx.localCount = static_cast<int>(ctx.local_slots.size());
    } else {
      global_local_slots.push_back(slot);
    }

    // Don't add to directValues since this needs to be loaded
  }

  addLoopInstructions(2);
}

void CodeGenVisitor::visitBlockStmt(Block *s) {
  // Two-pass approach to handle forward function references:
  // Pass 1: Declare all function signatures in this block
  // Pass 2: Process all statements (including function bodies)

  // Save current locals scope and global variables set
  auto beforeLocals = locals;
  auto beforeGlobals = globalVariables;

  // Track variables declared in this block for proper stack cleanup
  std::vector<std::string> blockVariables;

  // Increment block depth to track nesting
  blockDepth++;

  // Track this block's execution count for proper loop variable scoping
  blockExecutionCount[s]++;
  int currentBlockExecution = blockExecutionCount[s];

  // Pass 1: Find all function declarations and create their signatures
  for (auto &stmt : s->statements) {
    if (auto funcStmt = dynamic_cast<Function *>(stmt.get())) {
      declareFunctionSignature(funcStmt);
    }
  }

  // Pass 2: Process all statements normally
  for (auto &stmt : s->statements) {
    // Check if current basic block is already terminated
    // If so, skip remaining statements to avoid LLVM verification errors
    if (builder.GetInsertBlock() && builder.GetInsertBlock()->getTerminator()) {
      break;
    }

    // For variable declarations in re-executed blocks, we need special handling
    if (auto varStmt = dynamic_cast<Var *>(stmt.get())) {
      // Track this variable for stack cleanup
      blockVariables.push_back(varStmt->name.getLexeme());

      // Pass the block execution count to variable declaration
      // This will be used to create unique storage for loop body variables
      visitVarStmtWithExecution(varStmt, currentBlockExecution);
    } else if (auto funcStmt = dynamic_cast<Function *>(stmt.get())) {
      blockVariables.push_back(funcStmt->name.getLexeme());
      stmt->accept(this);
    } else {
      stmt->accept(this);
    }
  }

  // Decrement block depth when exiting block
  blockDepth--;

  // Restore previous locals scope and global variables set FIRST
  locals = std::move(beforeLocals);
  globalVariables = std::move(beforeGlobals);

  // CRITICAL FIX: Pop variables from stacks when exiting block scope
  for (const auto &varName : blockVariables) {
    if (!variableStacks[varName].empty()) {
      llvm::Value *slot = variableStacks[varName].back();
      bool wasCaptured = removeLocalSlot(slot);
      if (wasCaptured) {
        auto closeFn = mod.getFunction("elx_close_upvalues");
        auto *currentBlock = builder.GetInsertBlock();
        if (closeFn && currentBlock && !currentBlock->getTerminator()) {
          builder.CreateCall(closeFn, {slot});
        }
      }

      variableStacks[varName].pop_back();

      // CRITICAL FIX: If the stack becomes empty, remove the entry entirely
      // This allows global variables to be accessed correctly after local
      // shadowing ends
      if (variableStacks[varName].empty()) {
        variableStacks.erase(varName);
      }
    }
  }
}

void CodeGenVisitor::visitIfStmt(If *s) {
  s->condition->accept(this);
  llvm::Value *cond = value;
  addLoopInstructions(1);

  auto fn = builder.GetInsertBlock()->getParent();
  auto thenBB = llvm::BasicBlock::Create(ctx, "if.then", fn);
  auto elseBB = llvm::BasicBlock::Create(ctx, "if.else",
                                         fn); // Add to function immediately
  auto mergeBB = llvm::BasicBlock::Create(ctx, "if.end",
                                          fn); // Add to function immediately

  // Convert condition to boolean
  auto condI1 = isTruthy(cond);
  builder.CreateCondBr(condI1, thenBB, elseBB);
  addLoopInstructions(1);

  // Generate then branch
  builder.SetInsertPoint(thenBB);
  s->thenBranch->accept(this);
  // Only create branch if block doesn't already have a terminator
  if (!builder.GetInsertBlock()->getTerminator()) {
    builder.CreateBr(mergeBB);
    addLoopInstructions(1);
  }

  // Generate else branch
  builder.SetInsertPoint(elseBB);
  if (s->elseBranch) {
    s->elseBranch->accept(this);
  }
  // Only create branch if block doesn't already have a terminator
  if (!builder.GetInsertBlock()->getTerminator()) {
    builder.CreateBr(mergeBB);
    addLoopInstructions(1);
  }

  builder.SetInsertPoint(mergeBB);
  value = nilConst();
}

void CodeGenVisitor::visitWhileStmt(While *s) {
  auto estimated = estimateLoopBodyInstructions(s->body.get());
  if (estimated > MAX_LOOP_BODY_INSTRUCTIONS) {
    throw CompileError("Loop body too large.");
  }

  auto fn = builder.GetInsertBlock()->getParent();
  auto condBB = llvm::BasicBlock::Create(ctx, "while.cond", fn);
  auto bodyBB = llvm::BasicBlock::Create(ctx, "while.body", fn);
  auto endBB = llvm::BasicBlock::Create(ctx, "while.end", fn);

  builder.CreateBr(condBB);
  enterLoop();
  try {
    builder.SetInsertPoint(condBB);
    s->condition->accept(this);
    llvm::Value *cond = value;
    addLoopInstructions(1);

    // Use the simplified truthiness check
    auto condI1 = isTruthy(cond);
    builder.CreateCondBr(condI1, bodyBB, endBB);
    addLoopInstructions(1);

    builder.SetInsertPoint(bodyBB);
    s->body->accept(this);
    // Only create branch if block doesn't already have a terminator
    if (!builder.GetInsertBlock()->getTerminator()) {
      builder.CreateBr(condBB);
      addLoopInstructions(1);
    }

    builder.SetInsertPoint(endBB);
  } catch (...) {
    exitLoop();
    throw;
  }
  exitLoop();
  value = nilConst();
}

void CodeGenVisitor::declareFunctionSignature(Function *s) {
  const std::string &baseFuncName = s->name.getLexeme();

  std::string mapKey =
      function_map_key_override.empty() ? baseFuncName : function_map_key_override;

  // Check if function is already declared
  if (functions.find(mapKey) != functions.end()) {
    return; // Already declared
  }

  bool isMethod = (method_context_override != MethodContext::NONE);
  size_t arity = s->params.size() + (isMethod ? 1 : 0);
  ensureParameterLimit(arity);

  // Get upvalues for this function from resolver
  std::vector<std::string> upvalues;
  if (resolver_upvalues &&
      resolver_upvalues->find(s) != resolver_upvalues->end()) {
    upvalues = resolver_upvalues->at(s);
  }

  // Make function names unique to avoid JIT symbol conflicts
  static int functionCounter = 0;
  std::string funcName =
      baseFuncName + "_fn" + std::to_string(functionCounter++);

  // Create function type (all parameters and return value are Value types)
  std::vector<llvm::Type *> paramTypes(arity, llvmValueTy());

  // Add upvalue array parameter if needed
  if (!upvalues.empty()) {
    paramTypes.push_back(
        llvm::PointerType::get(llvmValueTy(), 0)); // upvalue array
  }

  llvm::FunctionType *funcType =
      llvm::FunctionType::get(llvmValueTy(), paramTypes, false);

  // Create the LLVM function declaration
  llvm::Function *llvmFunc = llvm::Function::Create(
      funcType, llvm::Function::ExternalLinkage, funcName, &mod);

  // Store the function in the global function table using base name
  functions[mapKey] = llvmFunc;

  // Track function for later object creation (methods are handled immediately)
  if (!isMethod && function_map_key_override.empty() && blockDepth == 0) {
    pendingFunctions.push_back({baseFuncName, arity});
  }
}

llvm::Value *CodeGenVisitor::createFunctionObject(const std::string &funcName,
                                                  llvm::Function *llvmFunc,
                                                  int arity) {
  // If we're currently inside a function, we cannot create function objects
  // as it creates cross-function IR references. Return nil and defer creation.
  if (currentFunction != nullptr) {
    return nilConst();
  }

  auto nameStr = builder.CreateGlobalStringPtr(funcName, "fname");
  auto arityConst = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), arity);
  auto funcPtr = builder.CreateBitCast(
      llvmFunc, llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0));

  auto allocFn = mod.getFunction("elx_allocate_function");
  if (!allocFn) {
    std::cerr << "    Error: elx_allocate_function not found\n";
    return nilConst();
  }

  auto funcObj =
      builder.CreateCall(allocFn, {nameStr, arityConst, funcPtr}, "funcobj");

  // Store in globals map for immediate access during compilation
  globals[funcName] = funcObj;

  // Store in persistent global environment for cross-line access
  auto setGlobalFuncFn = mod.getFunction("elx_set_global_function");
  if (setGlobalFuncFn) {
    auto funcNameStr = builder.CreateGlobalStringPtr(funcName, "func_name");
    builder.CreateCall(setGlobalFuncFn, {funcNameStr, funcObj});
  } else {
    std::cerr << "    Warning: elx_set_global_function not found\n";
  }

  return funcObj;
}

llvm::Value *CodeGenVisitor::createFunctionObjectImmediate(
    const std::string &funcName, llvm::Function *llvmFunc, int arity) {
  auto nameStr = builder.CreateGlobalStringPtr(funcName, "fname");
  auto arityConst = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), arity);
  auto funcPtr = builder.CreateBitCast(
      llvmFunc, llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0));

  auto allocFn = mod.getFunction("elx_allocate_function");
  if (!allocFn) {
    std::cerr << "    Error: elx_allocate_function not found\n";
    return nilConst();
  }

  return builder.CreateCall(allocFn, {nameStr, arityConst, funcPtr},
                            "funcobj");
}

void CodeGenVisitor::createGlobalFunctionObjects() {
  if (pendingFunctions.empty()) {
    return; // No functions to process
  }

  // Create a temporary global initialization function to hold the object
  // creation code
  auto voidTy = llvm::Type::getVoidTy(ctx);
  auto initFnTy = llvm::FunctionType::get(voidTy, {}, false);
  auto initFn = llvm::Function::Create(
      initFnTy, llvm::Function::ExternalLinkage, "__global_init", &mod);
  auto entryBB = llvm::BasicBlock::Create(ctx, "entry", initFn);

  // Save current state and switch to the init function
  llvm::Function *prevFunction = currentFunction;
  llvm::BasicBlock *prevBB = builder.GetInsertBlock();

  currentFunction = nullptr; // We're at global scope now
  builder.SetInsertPoint(entryBB);

  // Only create objects for pending functions that don't already exist
  for (const auto &pending : pendingFunctions) {
    const std::string &funcName = pending.first;
    int arity = pending.second;

    // Skip if already created
    if (globals.find(funcName) != globals.end()) {
      continue;
    }

    // Find the LLVM function
    auto funcIt = functions.find(funcName);
    if (funcIt == functions.end()) {
      continue; // Function not found, skip
    }

    llvm::Function *llvmFunc = funcIt->second;
    createFunctionObject(funcName, llvmFunc, arity);
  }

  // Finish the init function
  builder.CreateRetVoid();

  // Restore previous state
  currentFunction = prevFunction;
  if (prevBB) {
    builder.SetInsertPoint(prevBB);
  }

  // Clear pending functions
  pendingFunctions.clear();
}

void CodeGenVisitor::visitFunctionStmt(Function *s) {
  const std::string &baseFuncName = s->name.getLexeme();
  addLoopInstructions(1);

  MethodContext methodContext = method_context_override;
  method_context_override = MethodContext::NONE;

  std::string mapKey =
      function_map_key_override.empty() ? baseFuncName : function_map_key_override;
  function_map_key_override.clear();

  bool isMethod = (methodContext != MethodContext::NONE);

  size_t userParamCount = s->params.size();
  size_t totalParamCount = userParamCount + (isMethod ? 1 : 0);

  ensureParameterLimit(totalParamCount);

  bool nestedFunctionDeclaration =
      !isMethod && (currentFunction != nullptr || blockDepth > 0);
  llvm::Value *nestedFunctionSlot = nullptr;
  bool nestedSlotTrackedInOuterCtx = false;
  bool nestedSlotTrackedGlobally = false;

  // Get upvalues for this function from resolver
  std::vector<std::string> upvalues;
  if (resolver_upvalues &&
      resolver_upvalues->find(s) != resolver_upvalues->end()) {
    upvalues = resolver_upvalues->at(s);
  }

  // Filter out function parameters from upvalues (resolver bug workaround)
  std::vector<std::string> filtered_upvalues;
  for (const auto &upvalue_name : upvalues) {
    bool is_parameter = false;
    for (const auto &param : s->params) {
      if (param.getLexeme() == upvalue_name) {
        is_parameter = true;
        break;
      }
    }
    if (!is_parameter) {
      filtered_upvalues.push_back(upvalue_name);
    }
  }
  upvalues = filtered_upvalues;

  if (isMethod) {
    upvalues.erase(std::remove(upvalues.begin(), upvalues.end(), "this"),
                   upvalues.end());
  } else {
    bool hasSuperUpvalue = false;
    bool hasThisUpvalue = false;
    for (const auto &name : upvalues) {
      if (name == "super") {
        hasSuperUpvalue = true;
      } else if (name == "this") {
        hasThisUpvalue = true;
      }
    }

    if (hasSuperUpvalue && !hasThisUpvalue) {
      upvalues.push_back("this");
    }
  }

  // Get the already-declared function (from declareFunctionSignature)
  auto it = functions.find(mapKey);
  llvm::Function *llvmFunc;

  if (it != functions.end()) {
    // Function was already declared - reuse it
    llvmFunc = it->second;
  } else {
    // Fallback: declare it now (for cases where visitFunctionStmt is called
    // directly) - need to modify signature for upvalues
    function_map_key_override = mapKey;
    declareFunctionSignature(s);
    function_map_key_override.clear();

    // Check if declaration succeeded (might fail due to too many parameters)
    auto it2 = functions.find(mapKey);
    if (it2 == functions.end()) {
      // Declaration failed - set value to nil and return gracefully
      value = nilConst();
      return;
    } else {
      llvmFunc = it2->second;
    }
  }

  // Skip if function body is already defined
  if (!llvmFunc->empty()) {
    if (isMethod) {
      int methodArity = static_cast<int>(totalParamCount);
      if (upvalues.empty()) {
        value = createFunctionObjectImmediate(baseFuncName, llvmFunc,
                                              methodArity);
      } else {
        value = createDeferredClosure(llvmFunc, upvalues, methodArity,
                                      baseFuncName);
      }
    } else {
      // Create closure object instead of just nil
      if (upvalues.empty()) {
        value = createFunctionObject(baseFuncName, llvmFunc,
                                     static_cast<int>(userParamCount));
      } else {
        value = createClosureObject(llvmFunc, upvalues);
      }
    }
    return;
  }

  // Save current state before switching to function context
  llvm::Function *prevFunction = currentFunction;
  auto prevLocals = locals;
  auto prevDirectValues = directValues;
  auto prevVariableStacks = variableStacks; // Save variable stacks too
  llvm::BasicBlock *prevBB = builder.GetInsertBlock();

  if (nestedFunctionDeclaration) {
    llvm::Function *enclosingFn = prevFunction;
    if (!enclosingFn) {
      if (prevBB) {
        enclosingFn = prevBB->getParent();
      } else if (auto *insertBlock = builder.GetInsertBlock()) {
        enclosingFn = insertBlock->getParent();
      }
    }

    if (enclosingFn) {
      std::string slotName = baseFuncName + "_func_slot_" +
                             std::to_string(variableCounter++);
      nestedFunctionSlot =
          createStackAlloca(enclosingFn, llvmValueTy(), slotName);
      builder.CreateStore(nilConst(), nestedFunctionSlot);

      locals[baseFuncName] = nestedFunctionSlot;
      locals[baseFuncName + "_current"] = nestedFunctionSlot;
      variableStacks[baseFuncName].push_back(nestedFunctionSlot);

      if (!function_stack.empty()) {
        FunctionContext &outerCtx = function_stack.top();
        if (outerCtx.local_slots.size() >=
            static_cast<size_t>(MAX_USER_LOCAL_SLOTS)) {
          throw CompileError("Too many local variables in function.");
        }
        outerCtx.local_slots.push_back(nestedFunctionSlot);
        outerCtx.localCount = static_cast<int>(outerCtx.local_slots.size());
        nestedSlotTrackedInOuterCtx = true;
      } else {
        global_local_slots.push_back(nestedFunctionSlot);
        nestedSlotTrackedGlobally = true;
      }
    }
  }

  // Create new function context for closure support but DON'T switch yet
  FunctionContext funcCtx;
  funcCtx.llvm_function = llvmFunc;
  funcCtx.upvalues = upvalues;
  funcCtx.upvalueCount = static_cast<int>(upvalues.size());
  if (funcCtx.upvalueCount > MAX_UPVALUES) {
    throw CompileError("Too many closure variables in function.");
  }
  for (int i = 0; i < static_cast<int>(upvalues.size()); i++) {
    funcCtx.upvalue_indices[upvalues[i]] = i;
  }
  funcCtx.constantCount = 0;
  funcCtx.debug_name = baseFuncName;

  // Set up the function signature to understand upvalue parameter
  // but don't switch contexts yet
  auto tempLocals = locals; // Save current locals
  auto tempDirectValues = directValues;
  auto tempVariableStacks = variableStacks; // Save current variable stacks

  currentFunction = llvmFunc;
  locals.clear();
  directValues.clear();

  // Create entry block and set up parameters - but keep old locals for capture
  llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(ctx, "entry", llvmFunc);
  auto oldInsertPoint = builder.GetInsertBlock();
  builder.SetInsertPoint(entryBB);

  // Add parameters to local scope as direct values
  auto argIt = llvmFunc->arg_begin();
  if (isMethod && argIt != llvmFunc->arg_end()) {
    argIt->setName("this");
    locals["this"] = &*argIt;
    directValues.insert("this");
    ++argIt;
  }
  for (size_t i = 0; i < userParamCount && argIt != llvmFunc->arg_end(); ++i, ++argIt) {
    const std::string &paramName = s->params[i].getLexeme();
    argIt->setName(paramName);
    locals[paramName] = &*argIt;
    directValues.insert(paramName); // Mark as direct value (no alloca needed)
  }

  // Set up upvalue array parameter if needed
  if (!upvalues.empty() && argIt != llvmFunc->arg_end()) {
    argIt->setName("upvalues");
    funcCtx.upvalue_array = &*argIt;
  }

  // Restore old insert point and locals temporarily for closure creation
  builder.SetInsertPoint(oldInsertPoint);
  locals = tempLocals;
  directValues = tempDirectValues;
  variableStacks =
      tempVariableStacks; // Restore variable stacks for closure capture

  // Create closure while we still have access to outer scope
  llvm::Value *closureValue = nullptr;
  if (!upvalues.empty() && !isMethod) {
    closureValue = createDeferredClosure(llvmFunc, upvalues,
                                         static_cast<int>(userParamCount),
                                         baseFuncName);
  }

  // Now fully switch to function context
  currentFunction = llvmFunc;
  locals.clear();
  directValues.clear();
  builder.SetInsertPoint(entryBB);

  // Reset lexical storage tracking for the new function. Sharing the stacks
  // with the enclosing context causes inner functions to reference allocas
  // from other functions, triggering LLVM verifier failures when closures
  // capture locals.
  variableStacks.clear();

  std::vector<llvm::Value *> paramSlots;

  // Re-add parameters to local scope using stack slots so they can be captured
  argIt = llvmFunc->arg_begin();
  if (isMethod && argIt != llvmFunc->arg_end()) {
    argIt->setName("this");
    auto thisSlot = createStackAlloca(llvmFunc, llvmValueTy(), "this_param");
    builder.CreateStore(&*argIt, thisSlot);
    locals["this"] = thisSlot;
    locals["this_current"] = thisSlot;
    variableStacks["this"].push_back(thisSlot);
    paramSlots.push_back(thisSlot);
    ++argIt;
  }
  for (size_t i = 0; i < userParamCount && argIt != llvmFunc->arg_end(); ++i, ++argIt) {
    const std::string &paramName = s->params[i].getLexeme();
    argIt->setName(paramName);
    auto slotName = paramName + "_param";
    auto slot = createStackAlloca(llvmFunc, llvmValueTy(), slotName);
    builder.CreateStore(&*argIt, slot);
    locals[paramName] = slot;
    locals[paramName + "_current"] = slot;
    variableStacks[paramName].push_back(slot);
    paramSlots.push_back(slot);
  }

  // Set up upvalue array parameter if needed
  if (!upvalues.empty() && argIt != llvmFunc->arg_end()) {
    argIt->setName("upvalues");
    funcCtx.upvalue_array = &*argIt;
  }

  funcCtx.local_slots = paramSlots;
  funcCtx.localCount = static_cast<int>(paramSlots.size());
  if (funcCtx.localCount > MAX_USER_LOCAL_SLOTS) {
    throw CompileError("Too many local variables in function.");
  }
  funcCtx.method_context = methodContext;

  function_stack.push(funcCtx);

  LoopInstructionScopeReset loopInstructionReset(*this);

  // Generate function body
  try {
    s->body->accept(this);

    // If no explicit return and no terminator, return nil
    if (!builder.GetInsertBlock()->getTerminator()) {
      closeAllCapturedLocals();

      llvm::Value *implicitReturn = nilConst();
      if (methodContext == MethodContext::INITIALIZER) {
        llvm::Value *thisSlot = nullptr;
        auto thisIt = locals.find("this");
        if (thisIt != locals.end()) {
          thisSlot = thisIt->second;
        } else {
          auto currentIt = locals.find("this_current");
          if (currentIt != locals.end()) {
            thisSlot = currentIt->second;
          }
        }

        if (thisSlot) {
          implicitReturn =
              builder.CreateLoad(llvmValueTy(), thisSlot, "this");
        }
      }

      builder.CreateRet(implicitReturn);
    }
  } catch (const std::exception &e) {
    // Clean up and restore state
    llvmFunc->eraseFromParent();
    functions.erase(mapKey); // Remove from functions map
    function_stack.pop();
    currentFunction = prevFunction;
    locals = std::move(prevLocals);
    directValues = std::move(prevDirectValues);
    variableStacks = std::move(prevVariableStacks);
    if (prevBB) {
      builder.SetInsertPoint(prevBB);
    }
    if (nestedFunctionSlot) {
      if (nestedSlotTrackedInOuterCtx && !function_stack.empty()) {
        FunctionContext &outerCtx = function_stack.top();
        auto &slots = outerCtx.local_slots;
        auto it = std::find(slots.begin(), slots.end(), nestedFunctionSlot);
        if (it != slots.end()) {
          slots.erase(it);
          outerCtx.localCount = static_cast<int>(slots.size());
        }
        outerCtx.captured_slots.erase(nestedFunctionSlot);
      }
      if (nestedSlotTrackedGlobally) {
        auto it = std::find(global_local_slots.begin(),
                            global_local_slots.end(), nestedFunctionSlot);
        if (it != global_local_slots.end()) {
          global_local_slots.erase(it);
        }
        global_captured_slots.erase(nestedFunctionSlot);
      }
    }
    value = nilConst();
    throw;
  }

  // Verify the function
  if (llvm::verifyFunction(*llvmFunc, &llvm::errs())) {
    std::cerr << "LLVM verification failed for function: " << baseFuncName
              << "\n";
    llvmFunc->eraseFromParent();
    // Remove from functions map since verification failed
    functions.erase(mapKey);
    // Restore state
    function_stack.pop();
    currentFunction = prevFunction;
    locals = std::move(prevLocals);
    directValues = std::move(prevDirectValues);
    variableStacks = std::move(prevVariableStacks); // Restore variable stacks
    if (prevBB) {
      builder.SetInsertPoint(prevBB);
    }
    if (nestedFunctionSlot) {
      if (nestedSlotTrackedInOuterCtx && !function_stack.empty()) {
        FunctionContext &outerCtx = function_stack.top();
        auto &slots = outerCtx.local_slots;
        auto it = std::find(slots.begin(), slots.end(), nestedFunctionSlot);
        if (it != slots.end()) {
          slots.erase(it);
          outerCtx.localCount = static_cast<int>(slots.size());
        }
        outerCtx.captured_slots.erase(nestedFunctionSlot);
      }
      if (nestedSlotTrackedGlobally) {
        auto it = std::find(global_local_slots.begin(),
                            global_local_slots.end(), nestedFunctionSlot);
        if (it != global_local_slots.end()) {
          global_local_slots.erase(it);
        }
        global_captured_slots.erase(nestedFunctionSlot);
      }
    }
    value = nilConst();
    return;
  }

  // Function is already in the functions map from declareFunctionSignature
  // No need to add it again

  // Restore previous state BEFORE working with function objects
  function_stack.pop();
  currentFunction = prevFunction;
  locals = std::move(prevLocals);
  directValues = std::move(prevDirectValues);
  variableStacks = std::move(prevVariableStacks); // Restore variable stacks
  if (prevBB) {
    builder.SetInsertPoint(prevBB);
  }

  if (nestedFunctionSlot) {
    locals[baseFuncName] = nestedFunctionSlot;
    locals[baseFuncName + "_current"] = nestedFunctionSlot;
    variableStacks[baseFuncName].push_back(nestedFunctionSlot);
  }

  if (methodContext != MethodContext::NONE) {
    int methodArity = static_cast<int>(totalParamCount);
    llvm::Value *callable =
        createDeferredClosure(llvmFunc, upvalues, methodArity, baseFuncName);
    value = callable;
    return;
  }

  // Create closure object or function object
  if (upvalues.empty()) {
    // Treat declarations inside blocks as local even at top level so they don't
    // populate the global function table until hoisted explicitly.
    bool inNestedContext = prevFunction != nullptr || blockDepth > 0;

    if (inNestedContext) {
      // We're in a nested function context - create function object immediately
      auto nameStr = builder.CreateGlobalStringPtr(baseFuncName, "fname");
      auto arityConst = llvm::ConstantInt::get(
          llvm::Type::getInt32Ty(ctx), static_cast<int>(s->params.size()));
      auto funcPtr = builder.CreateBitCast(
          llvmFunc, llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0));

      auto allocFn = mod.getFunction("elx_allocate_function");
      if (!allocFn) {
        std::cerr << "    Error: elx_allocate_function not found\n";
        value = nilConst();
      } else {
        value = builder.CreateCall(allocFn, {nameStr, arityConst, funcPtr},
                                   "funcobj");
      }
    } else {
      // We're at global scope - use deferred creation
      pendingFunctions.push_back(
          {baseFuncName, static_cast<int>(s->params.size())});
      value = nilConst();
    }
  } else {
    // Use the pre-created closure value
    value = closureValue;
  }

  // Store the function/closure in locals (function declarations should behave
  // like variable declarations)
  if (value != nilConst()) {
    llvm::Value *targetSlot = nestedFunctionSlot;
    if (!targetSlot) {
      auto fn = builder.GetInsertBlock()->getParent();
      targetSlot = createStackAlloca(fn, llvmValueTy(), baseFuncName);
      locals[baseFuncName] = targetSlot;
      locals[baseFuncName + "_current"] = targetSlot;
      variableStacks[baseFuncName].push_back(targetSlot);
      if (!function_stack.empty()) {
        FunctionContext &outerCtx = function_stack.top();
        if (outerCtx.local_slots.size() >=
            static_cast<size_t>(MAX_USER_LOCAL_SLOTS)) {
          throw CompileError("Too many local variables in function.");
        }
        outerCtx.local_slots.push_back(targetSlot);
        outerCtx.localCount = static_cast<int>(outerCtx.local_slots.size());
      } else {
        global_local_slots.push_back(targetSlot);
      }
    }
    builder.CreateStore(value, targetSlot);
  }
}
void CodeGenVisitor::visitReturnStmt(Return *s) {
  bool isInitializer =
      !function_stack.empty() &&
      function_stack.top().method_context == MethodContext::INITIALIZER;

  llvm::Value *returnValue = nullptr;

  if (isInitializer) {
    if (s->value) {
      s->value->accept(this);
    }

    Token thisToken(TokenType::THIS, "this", std::monostate{},
                    s->keyword.getLine());
    Variable thisVar(thisToken);
    visitVariableExpr(&thisVar);
    returnValue = value;
  } else if (s->value) {
    s->value->accept(this);
    returnValue = value;
  } else {
    returnValue = nilConst();
  }

  closeAllCapturedLocals();
  builder.CreateRet(returnValue);
  value = returnValue;
  addLoopInstructions(1);
}

void CodeGenVisitor::emitLegacyGetExpr(Get *e, llvm::Value *objectValue,
                                       llvm::Value *nameValue) {
  auto tryGetFn = mod.getFunction("elx_try_get_instance_field");
  if (!tryGetFn) {
    value = nilConst();
    return;
  }

  if (!nameValue) {
    nameValue = stringConst(e->name.getLexeme(), true);
  }

  llvm::Function *fn = builder.GetInsertBlock()->getParent();
  auto outPtr = createStackAlloca(fn, llvmValueTy(), "get_field_out");
  builder.CreateStore(nilConst(), outPtr);

  auto status = builder.CreateCall(tryGetFn, {objectValue, nameValue, outPtr},
                                   "get_field_status");

  auto errorBB = llvm::BasicBlock::Create(ctx, "get.error", fn);
  auto dispatchBB = llvm::BasicBlock::Create(ctx, "get.dispatch", fn);
  auto successBB = llvm::BasicBlock::Create(ctx, "get.success", fn);
  auto fallbackBB = llvm::BasicBlock::Create(ctx, "get.fallback", fn);
  auto contBB = llvm::BasicBlock::Create(ctx, "get.cont", fn);

  auto errorCond = builder.CreateICmpEQ(
      status, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), -1),
      "get_field_failed");
  builder.CreateCondBr(errorCond, errorBB, dispatchBB);

  builder.SetInsertPoint(errorBB);
  auto emitErrorFn = mod.getFunction("elx_emit_runtime_error");
  if (emitErrorFn) {
    builder.CreateCall(emitErrorFn, {});
  }
  builder.CreateBr(contBB);
  llvm::BasicBlock *errorEndBB = builder.GetInsertBlock();

  builder.SetInsertPoint(dispatchBB);
  auto successCond = builder.CreateICmpEQ(
      status, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 1),
      "get_field_found");
  builder.CreateCondBr(successCond, successBB, fallbackBB);

  builder.SetInsertPoint(successBB);
  auto fieldValue = builder.CreateLoad(llvmValueTy(), outPtr, "get_field");
  builder.CreateBr(contBB);
  llvm::BasicBlock *successEndBB = builder.GetInsertBlock();

  builder.SetInsertPoint(fallbackBB);
  auto getClassFn = mod.getFunction("elx_get_instance_class");
  auto findMethodFn = mod.getFunction("elx_class_find_method");
  auto bindMethodFn = mod.getFunction("elx_bind_method");
  auto runtimeErrorSilentFn = mod.getFunction("elx_runtime_error_silent");
  emitErrorFn = mod.getFunction("elx_emit_runtime_error");

  llvm::BasicBlock *methodFoundBB = nullptr;
  llvm::Value *methodResult = nullptr;
  llvm::BasicBlock *methodMissingBB = nullptr;

  if (!getClassFn || !findMethodFn || !bindMethodFn) {
    if (emitErrorFn) {
      builder.CreateCall(emitErrorFn, {});
    } else {
      std::string message =
          "Undefined property '" + e->name.getLexeme() + "'.";
      emitRuntimeError(message);
    }
    builder.CreateBr(contBB);
    methodMissingBB = builder.GetInsertBlock();
  } else {
    auto classValue =
        builder.CreateCall(getClassFn, {objectValue}, "instance_class");
    auto methodValue = builder.CreateCall(findMethodFn, {classValue, nameValue},
                                          "super_method");

    auto methodIsNil =
        builder.CreateICmpEQ(methodValue, nilConst(), "method_missing");

    methodMissingBB = llvm::BasicBlock::Create(ctx, "get.no_method", fn);
    methodFoundBB = llvm::BasicBlock::Create(ctx, "get.method", fn);
    builder.CreateCondBr(methodIsNil, methodMissingBB, methodFoundBB);

    builder.SetInsertPoint(methodFoundBB);
    methodResult = builder.CreateCall(bindMethodFn,
                                      {objectValue, methodValue}, "bound_method");
    builder.CreateBr(contBB);
    methodFoundBB = builder.GetInsertBlock();

    builder.SetInsertPoint(methodMissingBB);
    std::string message =
        "Undefined property '" + e->name.getLexeme() + "'.";
    if (runtimeErrorSilentFn) {
      auto msgPtr =
          builder.CreateGlobalStringPtr(message, "missing_property_msg");
      builder.CreateCall(runtimeErrorSilentFn, {msgPtr});
    } else {
      emitRuntimeError(message);
    }
    if (emitErrorFn) {
      builder.CreateCall(emitErrorFn, {});
    }
    builder.CreateBr(contBB);
    methodMissingBB = builder.GetInsertBlock();
  }

  builder.SetInsertPoint(contBB);
  auto phi = builder.CreatePHI(llvmValueTy(), 4, "get.result");
  phi->addIncoming(fieldValue, successEndBB);
  if (methodFoundBB && methodResult) {
    phi->addIncoming(methodResult, methodFoundBB);
  }
  phi->addIncoming(nilConst(), methodMissingBB);
  phi->addIncoming(nilConst(), errorEndBB);

  value = phi;
  checkRuntimeError(value);
}

void CodeGenVisitor::emitLegacySetExpr(Set *e, llvm::Value *objectValue) {
  auto hasErrorFn = mod.getFunction("elx_has_runtime_error");
  auto setFieldFn = mod.getFunction("elx_set_instance_field");
  if (!setFieldFn || !hasErrorFn) {
    e->value->accept(this);
    llvm::Value *assignedValue = value;
    if (setFieldFn) {
      auto nameValue = stringConst(e->name.getLexeme(), true);
      value = builder.CreateCall(setFieldFn,
                                 {objectValue, nameValue, assignedValue},
                                 "set_field");
      checkRuntimeError(value);
    } else {
      value = assignedValue;
    }
    return;
  }

  auto errorFlag = builder.CreateCall(hasErrorFn, {}, "set_object_error");
  auto hasError = builder.CreateICmpNE(
      errorFlag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0),
      "object_error");

  llvm::Function *fn = builder.GetInsertBlock()->getParent();
  auto skipValueBB = llvm::BasicBlock::Create(ctx, "set.skip", fn);
  auto evalValueBB = llvm::BasicBlock::Create(ctx, "set.eval", fn);
  auto contBB = llvm::BasicBlock::Create(ctx, "set.cont", fn);

  builder.CreateCondBr(hasError, skipValueBB, evalValueBB);

  builder.SetInsertPoint(evalValueBB);
  e->value->accept(this);
  llvm::Value *assignedValue = value;
  auto nameValue = stringConst(e->name.getLexeme(), true);
  llvm::Value *setResult = builder.CreateCall(
      setFieldFn, {objectValue, nameValue, assignedValue}, "set_field");
  checkRuntimeError(setResult);
  llvm::Value *successValue = value;
  builder.CreateBr(contBB);
  llvm::BasicBlock *successBB = builder.GetInsertBlock();

  builder.SetInsertPoint(skipValueBB);
  llvm::Value *skipValue = nilConst();
  builder.CreateBr(contBB);
  llvm::BasicBlock *skipEndBB = builder.GetInsertBlock();

  builder.SetInsertPoint(contBB);
  auto phi = builder.CreatePHI(llvmValueTy(), 2, "set.result");
  phi->addIncoming(successValue, successBB);
  phi->addIncoming(skipValue, skipEndBB);
  value = phi;
}

void CodeGenVisitor::visitGetExpr(Get *e) {
  e->object->accept(this);
  llvm::Value *objectValue = value;

  auto nameValue = stringConst(e->name.getLexeme(), true);

  auto shapeFn = mod.getFunction("elx_instance_shape_ptr");
  auto fieldsFn = mod.getFunction("elx_instance_field_values_ptr");
  auto presenceFn = mod.getFunction("elx_instance_field_presence_ptr");
  auto slowFn = mod.getFunction("elx_get_property_slow");

  if (!shapeFn || !fieldsFn || !presenceFn || !slowFn) {
    emitLegacyGetExpr(e, objectValue, nameValue);
    return;
  }

  auto cacheGV = getPropertyCacheGlobal("get", e);
  auto cacheTy = getPropertyCacheType();
  auto cachePtr = cacheGV;

  llvm::Function *fn = builder.GetInsertBlock()->getParent();
  auto slowBB = llvm::BasicBlock::Create(ctx, "get.slow", fn);
  auto contBB = llvm::BasicBlock::Create(ctx, "get.cont", fn);

  auto shapeValue = builder.CreateCall(shapeFn, {objectValue}, "instance_shape");

  auto int32Ty = llvm::Type::getInt32Ty(ctx);
  auto sizePtr = builder.CreateStructGEP(cacheTy, cachePtr, 0, "cache_size_ptr");
  auto entriesPtr =
      builder.CreateStructGEP(cacheTy, cachePtr, 1, "cache_entries_ptr");
  auto entriesArrayTy =
      llvm::cast<llvm::ArrayType>(cacheTy->getElementType(1));
  auto entryTy =
      llvm::cast<llvm::StructType>(entriesArrayTy->getElementType());
  auto shapePtrTy = llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0);

  auto shapeNull = builder.CreateIsNull(shapeValue, "shape_null");
  auto guardBB = llvm::BasicBlock::Create(ctx, "get.cache.start", fn);
  builder.CreateCondBr(shapeNull, slowBB, guardBB);

  builder.SetInsertPoint(guardBB);
  auto sizeVal = builder.CreateLoad(int32Ty, sizePtr, "cache_size");

  llvm::BasicBlock *startBB = builder.GetInsertBlock();
  std::vector<std::pair<llvm::BasicBlock *, llvm::Value *>> phiIncoming;
  llvm::BasicBlock *fallback = slowBB;

  for (int idx = static_cast<int>(PROPERTY_CACHE_MAX_SIZE) - 1; idx >= 0; --idx) {
    auto checkBB =
        llvm::BasicBlock::Create(ctx, "get.cache." + std::to_string(idx) + ".check", fn);
    auto shapeCheckBB =
        llvm::BasicBlock::Create(ctx, "get.cache." + std::to_string(idx) + ".shape", fn);
    auto fastBB =
        llvm::BasicBlock::Create(ctx, "get.cache." + std::to_string(idx) + ".fast", fn);
    auto idxConst = builder.getInt32(idx);

    builder.SetInsertPoint(checkBB);
    auto hasEntry = builder.CreateICmpUGT(sizeVal, idxConst, "cache_has");
    builder.CreateCondBr(hasEntry, shapeCheckBB, fallback);

    builder.SetInsertPoint(shapeCheckBB);
    auto entryPtr = builder.CreateInBoundsGEP(
        entriesArrayTy, entriesPtr, {builder.getInt32(0), idxConst},
        "cache_entry_ptr");
    auto shapeElemPtr = builder.CreateStructGEP(entryTy, entryPtr, 0,
                                                "cache_shape_ptr");
    auto cachedShape =
        builder.CreateLoad(shapePtrTy, shapeElemPtr, "cached_shape");
    auto shapeMatch =
        builder.CreateICmpEQ(shapeValue, cachedShape, "shape_match");
    builder.CreateCondBr(shapeMatch, fastBB, fallback);

    builder.SetInsertPoint(fastBB);
    auto slotElemPtr = builder.CreateStructGEP(entryTy, entryPtr, 1,
                                               "cache_slot_ptr");
    auto slotVal = builder.CreateLoad(int32Ty, slotElemPtr, "cached_slot");
    auto fieldsPtr =
        builder.CreateCall(fieldsFn, {objectValue}, "fields_ptr");
    auto fieldsNull = builder.CreateIsNull(fieldsPtr, "fields_null");
    auto fieldsBB = llvm::BasicBlock::Create(
        ctx, "get.cache." + std::to_string(idx) + ".fields", fn);
    builder.CreateCondBr(fieldsNull, fallback, fieldsBB);

    builder.SetInsertPoint(fieldsBB);
    auto slotIdx64 =
        builder.CreateZExt(slotVal, builder.getInt64Ty(), "slot_idx64");
    auto fieldPtr =
        builder.CreateInBoundsGEP(llvmValueTy(), fieldsPtr, slotIdx64, "field_ptr");
    auto presencePtr =
        builder.CreateCall(presenceFn, {objectValue}, "presence_ptr");
    auto presenceNull = builder.CreateIsNull(presencePtr, "presence_null");
    auto presenceBB = llvm::BasicBlock::Create(
        ctx, "get.cache." + std::to_string(idx) + ".presence", fn);
    builder.CreateCondBr(presenceNull, fallback, presenceBB);

    builder.SetInsertPoint(presenceBB);
    auto presenceElemPtr = builder.CreateInBoundsGEP(
        llvm::Type::getInt8Ty(ctx), presencePtr, slotIdx64,
        "presence_elem_ptr");
    auto presenceVal = builder.CreateLoad(llvm::Type::getInt8Ty(ctx),
                                          presenceElemPtr, "presence_val");
    auto isPresent =
        builder.CreateICmpNE(presenceVal, builder.getInt8(0), "presence_set");
    auto hitBB = llvm::BasicBlock::Create(
        ctx, "get.cache." + std::to_string(idx) + ".hit", fn);
    builder.CreateCondBr(isPresent, hitBB, fallback);

    builder.SetInsertPoint(hitBB);
    auto cachedValue =
        builder.CreateLoad(llvmValueTy(), fieldPtr, "cached_value");
#ifdef ELOXIR_ENABLE_CACHE_STATS
    if (auto *hitFn = mod.getFunction("elx_cache_stats_record_property_hit")) {
      builder.CreateCall(hitFn, {builder.getInt32(0)});
    }
#endif
    builder.CreateBr(contBB);
    phiIncoming.emplace_back(builder.GetInsertBlock(), cachedValue);

    fallback = checkBB;
  }

  builder.SetInsertPoint(startBB);
  builder.CreateBr(fallback);

  builder.SetInsertPoint(slowBB);
  auto slowResult = builder.CreateCall(
      slowFn,
      {objectValue, nameValue, cachePtr,
       builder.getInt32(static_cast<int>(PROPERTY_CACHE_MAX_SIZE))},
      "slow_get");
  builder.CreateBr(contBB);
  phiIncoming.emplace_back(builder.GetInsertBlock(), slowResult);

  builder.SetInsertPoint(contBB);
  auto phi = builder.CreatePHI(llvmValueTy(), phiIncoming.size(), "get.result");
  for (auto &incoming : phiIncoming) {
    phi->addIncoming(incoming.second, incoming.first);
  }

  value = phi;
  checkRuntimeError(value);
}

void CodeGenVisitor::visitSetExpr(Set *e) {
  e->object->accept(this);
  llvm::Value *objectValue = value;

  auto hasErrorFn = mod.getFunction("elx_has_runtime_error");
  auto shapeFn = mod.getFunction("elx_instance_shape_ptr");
  auto fieldsFn = mod.getFunction("elx_instance_field_values_ptr");
  auto presenceFn = mod.getFunction("elx_instance_field_presence_ptr");
  auto slowFn = mod.getFunction("elx_set_property_slow");

  if (!shapeFn || !fieldsFn || !presenceFn || !slowFn || !hasErrorFn) {
    emitLegacySetExpr(e, objectValue);
    return;
  }

  auto errorFlag = builder.CreateCall(hasErrorFn, {}, "set_object_error");
  auto hasError = builder.CreateICmpNE(
      errorFlag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0),
      "object_error");

  llvm::Function *fn = builder.GetInsertBlock()->getParent();
  auto skipValueBB = llvm::BasicBlock::Create(ctx, "set.skip", fn);
  auto evalValueBB = llvm::BasicBlock::Create(ctx, "set.eval", fn);
  auto contBB = llvm::BasicBlock::Create(ctx, "set.cont", fn);

  builder.CreateCondBr(hasError, skipValueBB, evalValueBB);

  builder.SetInsertPoint(evalValueBB);
  e->value->accept(this);
  llvm::Value *assignedValue = value;
  auto nameValue = stringConst(e->name.getLexeme(), true);

  auto cacheGV = getPropertyCacheGlobal("set", e);
  auto cacheTy = getPropertyCacheType();
  auto cachePtr = cacheGV;

  auto slowBB = llvm::BasicBlock::Create(ctx, "set.slow", fn);
  auto valueContBB = llvm::BasicBlock::Create(ctx, "set.value.cont", fn);

  auto shapeValue = builder.CreateCall(shapeFn, {objectValue}, "instance_shape");

  auto int32Ty = llvm::Type::getInt32Ty(ctx);
  auto sizePtr = builder.CreateStructGEP(cacheTy, cachePtr, 0, "cache_size_ptr");
  auto entriesPtr =
      builder.CreateStructGEP(cacheTy, cachePtr, 1, "cache_entries_ptr");
  auto entriesArrayTy =
      llvm::cast<llvm::ArrayType>(cacheTy->getElementType(1));
  auto entryTy =
      llvm::cast<llvm::StructType>(entriesArrayTy->getElementType());
  auto shapePtrTy = llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0);

  auto shapeNull = builder.CreateIsNull(shapeValue, "shape_null");
  auto guardBB = llvm::BasicBlock::Create(ctx, "set.cache.start", fn);
  builder.CreateCondBr(shapeNull, slowBB, guardBB);

  builder.SetInsertPoint(guardBB);
  auto sizeVal = builder.CreateLoad(int32Ty, sizePtr, "cache_size");

  llvm::BasicBlock *startBB = builder.GetInsertBlock();
  std::vector<std::pair<llvm::BasicBlock *, llvm::Value *>> valuePhi;
  llvm::BasicBlock *fallback = slowBB;

  for (int idx = static_cast<int>(PROPERTY_CACHE_MAX_SIZE) - 1; idx >= 0; --idx) {
    auto checkBB =
        llvm::BasicBlock::Create(ctx, "set.cache." + std::to_string(idx) + ".check", fn);
    auto shapeCheckBB =
        llvm::BasicBlock::Create(ctx, "set.cache." + std::to_string(idx) + ".shape", fn);
    auto fastBB =
        llvm::BasicBlock::Create(ctx, "set.cache." + std::to_string(idx) + ".fast", fn);
    auto idxConst = builder.getInt32(idx);

    builder.SetInsertPoint(checkBB);
    auto hasEntry = builder.CreateICmpUGT(sizeVal, idxConst, "cache_has");
    builder.CreateCondBr(hasEntry, shapeCheckBB, fallback);

    builder.SetInsertPoint(shapeCheckBB);
    auto entryPtr = builder.CreateInBoundsGEP(
        entriesArrayTy, entriesPtr, {builder.getInt32(0), idxConst},
        "cache_entry_ptr");
    auto shapeElemPtr = builder.CreateStructGEP(entryTy, entryPtr, 0,
                                                "cache_shape_ptr");
    auto cachedShape =
        builder.CreateLoad(shapePtrTy, shapeElemPtr, "cached_shape");
    auto shapeMatch =
        builder.CreateICmpEQ(shapeValue, cachedShape, "shape_match");
    builder.CreateCondBr(shapeMatch, fastBB, fallback);

    builder.SetInsertPoint(fastBB);
    auto slotElemPtr = builder.CreateStructGEP(entryTy, entryPtr, 1,
                                               "cache_slot_ptr");
    auto slotVal = builder.CreateLoad(int32Ty, slotElemPtr, "cached_slot");
    auto fieldsPtr =
        builder.CreateCall(fieldsFn, {objectValue}, "fields_ptr");
    auto fieldsNull = builder.CreateIsNull(fieldsPtr, "fields_null");
    auto fieldsBB = llvm::BasicBlock::Create(
        ctx, "set.cache." + std::to_string(idx) + ".fields", fn);
    builder.CreateCondBr(fieldsNull, fallback, fieldsBB);

    builder.SetInsertPoint(fieldsBB);
    auto slotIdx64 =
        builder.CreateZExt(slotVal, builder.getInt64Ty(), "slot_idx64");
    auto fieldPtr =
        builder.CreateInBoundsGEP(llvmValueTy(), fieldsPtr, slotIdx64, "field_ptr");
    auto presencePtr =
        builder.CreateCall(presenceFn, {objectValue}, "presence_ptr");
    auto presenceNull = builder.CreateIsNull(presencePtr, "presence_null");
    auto presenceBB = llvm::BasicBlock::Create(
        ctx, "set.cache." + std::to_string(idx) + ".presence", fn);
    builder.CreateCondBr(presenceNull, fallback, presenceBB);

    builder.SetInsertPoint(presenceBB);
    auto presenceElemPtr = builder.CreateInBoundsGEP(
        llvm::Type::getInt8Ty(ctx), presencePtr, slotIdx64,
        "presence_elem_ptr");
    builder.CreateStore(assignedValue, fieldPtr);
    builder.CreateStore(builder.getInt8(1), presenceElemPtr);
#ifdef ELOXIR_ENABLE_CACHE_STATS
    if (auto *hitFn = mod.getFunction("elx_cache_stats_record_property_hit")) {
      builder.CreateCall(hitFn, {builder.getInt32(1)});
    }
#endif
    builder.CreateBr(valueContBB);
    valuePhi.emplace_back(builder.GetInsertBlock(), assignedValue);

    fallback = checkBB;
  }

  builder.SetInsertPoint(startBB);
  builder.CreateBr(fallback);

  builder.SetInsertPoint(slowBB);
  auto slowResult = builder.CreateCall(
      slowFn,
      {objectValue, nameValue, assignedValue, cachePtr,
       builder.getInt32(static_cast<int>(PROPERTY_CACHE_MAX_SIZE))},
      "slow_set");
  builder.CreateBr(valueContBB);
  valuePhi.emplace_back(builder.GetInsertBlock(), slowResult);

  builder.SetInsertPoint(valueContBB);
  auto evalPhi =
      builder.CreatePHI(llvmValueTy(), valuePhi.size(), "set.value.result");
  for (auto &incoming : valuePhi) {
    evalPhi->addIncoming(incoming.second, incoming.first);
  }
  builder.CreateBr(contBB);
  llvm::BasicBlock *successBB = builder.GetInsertBlock();
  llvm::Value *successValue = evalPhi;

  builder.SetInsertPoint(skipValueBB);
  llvm::Value *skipValue = nilConst();
  builder.CreateBr(contBB);
  llvm::BasicBlock *skipEndBB = builder.GetInsertBlock();

  builder.SetInsertPoint(contBB);
  auto phi = builder.CreatePHI(llvmValueTy(), 2, "set.result");
  phi->addIncoming(successValue, successBB);
  phi->addIncoming(skipValue, skipEndBB);
  value = phi;
  checkRuntimeError(value);
}

void CodeGenVisitor::visitThisExpr(This *e) {
  auto stackIt = variableStacks.find("this");
  if (stackIt != variableStacks.end() && !stackIt->second.empty()) {
    llvm::Value *slot = stackIt->second.back();
    value = builder.CreateLoad(llvmValueTy(), slot, "this");
    return;
  }

  auto currentIt = locals.find("this_current");
  if (currentIt != locals.end()) {
    value = builder.CreateLoad(llvmValueTy(), currentIt->second, "this");
    return;
  }

  Variable fakeVar(e->keyword);
  visitVariableExpr(&fakeVar);
}

void CodeGenVisitor::visitSuperExpr(Super *e) {
  Variable superVar(e->keyword);
  visitVariableExpr(&superVar);
  llvm::Value *superClassValue = value;

  Token thisToken(TokenType::THIS, "this", std::monostate{},
                  e->keyword.getLine());
  Variable thisVar(thisToken);
  visitVariableExpr(&thisVar);
  llvm::Value *thisValue = value;

  auto findMethodFn = mod.getFunction("elx_class_find_method");
  auto bindMethodFn = mod.getFunction("elx_bind_method");
  if (!findMethodFn || !bindMethodFn) {
    value = nilConst();
    return;
  }

  auto methodName = stringConst(e->method.getLexeme(), true);
  auto methodValue = builder.CreateCall(findMethodFn,
                                        {superClassValue, methodName},
                                        "super_method");

  value = builder.CreateCall(bindMethodFn, {thisValue, methodValue},
                             "bound_super");
  checkRuntimeError(value);
}

void CodeGenVisitor::visitClassStmt(Class *s) {
  const std::string className = s->name.getLexeme();
  addLoopInstructions(1);

  auto currentBlock = builder.GetInsertBlock();
  llvm::Function *enclosingFunction =
      currentBlock ? currentBlock->getParent() : nullptr;

  bool isGlobal = ((currentFunction == nullptr) ||
                   (enclosingFunction &&
                    enclosingFunction->getName().str().find("__expr") == 0)) &&
                  (blockDepth == 0);

  llvm::Value *superValue = nilConst();
  bool hasSuper = static_cast<bool>(s->superclass);
  if (hasSuper) {
    s->superclass->accept(this);
    superValue = value;

    auto validateSuperFn = mod.getFunction("elx_validate_superclass");
    if (validateSuperFn) {
      auto validated = builder.CreateCall(validateSuperFn, {superValue},
                                          "validated_super");
      checkRuntimeError(validated);
      superValue = value;
    }
  }

  auto makeAllocaInEntry = [&](const std::string &slotName) -> llvm::Value * {
    llvm::Function *targetFn = enclosingFunction;
    if (!targetFn) {
      if (auto *block = builder.GetInsertBlock()) {
        targetFn = block->getParent();
      }
    }
    if (!targetFn) {
      return builder.CreateAlloca(llvmValueTy(), nullptr, slotName.c_str());
    }
    return createStackAlloca(targetFn, llvmValueTy(), slotName);
  };

  int slotId = variableCounter++;
  std::string slotName =
      className + "_class_slot_" + std::to_string(slotId);
  llvm::Value *classSlot = makeAllocaInEntry(slotName);
  builder.CreateStore(nilConst(), classSlot);

  std::string uniqueKey = className + "#" + std::to_string(blockDepth) + "#" +
                          std::to_string(slotId);
  locals[uniqueKey] = classSlot;
  if (variableStacks[className].empty()) {
    locals[className + "_current"] = classSlot;
  }
  variableStacks[className].push_back(classSlot);

  if (isGlobal) {
    globalVariables.insert(className);
  }

  struct SyntheticBindingState {
    std::string name;
    size_t previousStackSize;
    bool hadLocal;
    llvm::Value *previousLocal;
    bool hadCurrent;
    llvm::Value *previousCurrent;
  };

  std::vector<SyntheticBindingState> syntheticBindings;

  auto pushSyntheticBinding = [&](const std::string &name,
                                  llvm::Value *initialValue) {
    SyntheticBindingState state{name, variableStacks[name].size(),
                                false, nullptr, false, nullptr};

    auto itLocal = locals.find(name);
    if (itLocal != locals.end()) {
      state.hadLocal = true;
      state.previousLocal = itLocal->second;
    }
    auto itCurrent = locals.find(name + "_current");
    if (itCurrent != locals.end()) {
      state.hadCurrent = true;
      state.previousCurrent = itCurrent->second;
    }

    int bindingId = variableCounter++;
    std::string bindingName =
        name + "_binding_" + std::to_string(bindingId);
    llvm::Value *slot = makeAllocaInEntry(bindingName);
    builder.CreateStore(initialValue, slot);
    locals[name] = slot;
    locals[name + "_current"] = slot;
    variableStacks[name].push_back(slot);

    syntheticBindings.push_back(state);
  };

  if (enclosingFunction) {
    pushSyntheticBinding("this", nilConst());
    if (hasSuper) {
      pushSyntheticBinding("super", superValue);
    }
  }

  std::vector<std::pair<std::string, llvm::Value *>> methodTable;
  methodTable.reserve(s->methods.size());

  for (auto &method : s->methods) {
    MethodContext methodCtx =
        (method->name.getLexeme() == "init") ? MethodContext::INITIALIZER
                                               : MethodContext::METHOD;

    auto previousOverride = method_context_override;
    auto previousKeyOverride = function_map_key_override;
    method_context_override = methodCtx;
    std::string uniqueMapKey =
        className + "::" + method->name.getLexeme() + "#" +
        std::to_string(reinterpret_cast<uintptr_t>(method.get()));
    function_map_key_override = uniqueMapKey;

    declareFunctionSignature(method.get());

    llvm::Value *methodValue = nullptr;
    try {
      method->accept(this);
      methodValue = value;
    } catch (...) {
      function_map_key_override = previousKeyOverride;
      method_context_override = previousOverride;
      throw;
    }

    method_context_override = previousOverride;
    function_map_key_override = previousKeyOverride;
    methodTable.emplace_back(method->name.getLexeme(), methodValue);
  }

  auto classNameValue = stringConst(className, true);
  auto allocateClassFn = mod.getFunction("elx_allocate_class");
  if (!allocateClassFn) {
    value = nilConst();
    return;
  }

  llvm::Value *classValue = builder.CreateCall(
      allocateClassFn,
      {classNameValue, hasSuper ? superValue : nilConst()}, "klass");
  checkRuntimeError(classValue);

  builder.CreateStore(classValue, classSlot);
  globals[className] = classValue;

  if (isGlobal) {
    auto setGlobalVarFn = mod.getFunction("elx_set_global_variable");
    if (setGlobalVarFn) {
      auto nameStr = builder.CreateGlobalStringPtr(className, "class_name");
      builder.CreateCall(setGlobalVarFn, {nameStr, classValue});
    }
  }

  auto addMethodFn = mod.getFunction("elx_class_add_method");
  llvm::Value *previousClassValue = current_class_value;
  current_class_value = classValue;
  if (addMethodFn) {
    for (const auto &entry : methodTable) {
      auto methodNameValue = stringConst(entry.first, true);
      builder.CreateCall(addMethodFn,
                         {classValue, methodNameValue, entry.second});
    }
  }
  current_class_value = previousClassValue;

  for (auto it = syntheticBindings.rbegin(); it != syntheticBindings.rend();
       ++it) {
    const auto &state = *it;
    auto &stack = variableStacks[state.name];
    while (stack.size() > state.previousStackSize) {
      stack.pop_back();
    }
    if (stack.empty()) {
      variableStacks.erase(state.name);
    }

    if (state.hadLocal) {
      locals[state.name] = state.previousLocal;
    } else {
      locals.erase(state.name);
    }

    if (state.hadCurrent) {
      locals[state.name + "_current"] = state.previousCurrent;
    } else {
      locals.erase(state.name + "_current");
    }
  }

  value = classValue;
}

void CodeGenVisitor::closeAllCapturedLocals() {
  if (function_stack.empty()) {
    return;
  }

  FunctionContext &ctx = function_stack.top();
  if (ctx.captured_slots.empty()) {
    return;
  }

  auto closeFn = mod.getFunction("elx_close_upvalues");
  if (!closeFn) {
    return;
  }

  for (auto it = ctx.local_slots.rbegin(); it != ctx.local_slots.rend(); ++it) {
    llvm::Value *slot = *it;
    if (ctx.captured_slots.count(slot) > 0) {
      builder.CreateCall(closeFn, {slot});
    }
  }
}

bool CodeGenVisitor::removeLocalSlot(llvm::Value *slot) {
  bool captured = false;

  bool removedFromContext = false;

  if (!function_stack.empty()) {
    FunctionContext &ctx = function_stack.top();
    if (!ctx.local_slots.empty()) {
      auto it = std::find(ctx.local_slots.rbegin(), ctx.local_slots.rend(), slot);
      if (it != ctx.local_slots.rend()) {
        ctx.local_slots.erase(std::next(it).base());
        removedFromContext = true;
      }
    }
    captured = ctx.captured_slots.erase(slot) > 0;
    if (removedFromContext) {
      ctx.localCount = static_cast<int>(ctx.local_slots.size());
    }
  }

  if (!global_local_slots.empty()) {
    auto it = std::find(global_local_slots.rbegin(), global_local_slots.rend(), slot);
    if (it != global_local_slots.rend()) {
      global_local_slots.erase(std::next(it).base());
    }
  }

  if (global_captured_slots.erase(slot) > 0) {
    captured = true;
  }

  return captured;
}

void CodeGenVisitor::enterLoop() { loopInstructionCounts.push_back(0); }

void CodeGenVisitor::exitLoop() {
  if (!loopInstructionCounts.empty()) {
    loopInstructionCounts.pop_back();
  }
}

void CodeGenVisitor::addLoopInstructions(std::size_t count) {
  if (loopInstructionCounts.empty()) {
    return;
  }

  std::size_t current = loopInstructionCounts.back();
  std::size_t total = saturatingLoopAdd(current, count);
  loopInstructionCounts.back() = total;
  if (total > MAX_LOOP_BODY_INSTRUCTIONS) {
    throw CompileError("Loop body too large.");
  }
}

CodeGenVisitor::LoopInstructionScopeReset::LoopInstructionScopeReset(
    CodeGenVisitor &visitor)
    : visitor(visitor), depth(visitor.loopInstructionCounts.size()) {}

CodeGenVisitor::LoopInstructionScopeReset::~LoopInstructionScopeReset() {
  while (visitor.loopInstructionCounts.size() > depth) {
    visitor.loopInstructionCounts.pop_back();
  }
}

void CodeGenVisitor::checkRuntimeError(llvm::Value *returnValue) {
  // Check if there's a runtime error and return nil if so
  llvm::Function *hasErrorFn = mod.getFunction("elx_has_runtime_error");
  if (!hasErrorFn) {
    if (returnValue)
      value = returnValue;
    return; // No error checking infrastructure available
  }

  // Check for runtime error
  auto hasError = builder.CreateCall(hasErrorFn, {}, "has_error");
  auto hasErrorBool = builder.CreateICmpNE(
      hasError, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0),
      "error_check");

  // Use select to return nil if there's an error, otherwise return the original
  // value
  if (returnValue) {
    value = builder.CreateSelect(hasErrorBool, nilConst(), returnValue,
                                 "error_safe_value");
  } else {
    // For void operations, just check but don't change value
    // The error will be caught at the REPL level
  }
}

void CodeGenVisitor::recordConstant() {
  if (!function_stack.empty()) {
    FunctionContext &ctx = function_stack.top();
    if (ctx.constantCount >= MAX_CONSTANTS) {
      throw CompileError("Too many constants in one chunk.");
    }
    ctx.constantCount++;
  } else {
    if (globalConstantCount >= MAX_CONSTANTS) {
      throw CompileError("Too many constants in one chunk.");
    }
    globalConstantCount++;
  }
}

void CodeGenVisitor::ensureParameterLimit(size_t arity) {
  if (arity > static_cast<size_t>(MAX_PARAMETERS)) {
    throw CompileError("Can't have more than 255 parameters.");
  }
}

bool CodeGenVisitor::isUpvalue(const std::string &name) {
  if (function_stack.empty()) {
    return false;
  }

  const FunctionContext &current_ctx = function_stack.top();
  return current_ctx.upvalue_indices.find(name) !=
         current_ctx.upvalue_indices.end();
}

llvm::Value *
CodeGenVisitor::createClosureObject(llvm::Function *func,
                                    const std::vector<std::string> &upvalues) {
  // 1. Create function object
  auto func_obj = createFunctionObject(
      "", func, func->arg_size() - (upvalues.empty() ? 0 : 1));

  // 2. Allocate closure with upvalue slots
  auto alloc_closure_fn = mod.getFunction("elx_allocate_closure");
  auto upvalue_count =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), upvalues.size());
  auto closure_obj =
      builder.CreateCall(alloc_closure_fn, {func_obj, upvalue_count});

  // 3. Capture each upvalue
  for (int i = 0; i < static_cast<int>(upvalues.size()); i++) {
    auto upvalue_value = captureUpvalue(upvalues[i]);
    auto set_upvalue_fn = mod.getFunction("elx_set_closure_upvalue");
    auto index = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), i);
    builder.CreateCall(set_upvalue_fn, {closure_obj, index, upvalue_value});
  }

  return closure_obj;
}

llvm::Value *CodeGenVisitor::createDeferredClosureWithCapturedUpvalues(
    llvm::Function *func, const std::vector<std::string> &upvalues,
    const std::unordered_map<std::string, llvm::Value *> &capturedUpvalues,
    int arity, const std::string &funcName) {
  // Use the existing runtime API for closure creation with pre-captured upvalue
  // values

  // Build function object first
  int llvm_arity = arity + (upvalues.empty() ? 0 : 1);

  auto nameStr = builder.CreateGlobalStringPtr(funcName, "fname");
  auto arityConst =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), llvm_arity);
  auto funcPtr = builder.CreateBitCast(
      func, llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0));

  auto allocFn = mod.getFunction("elx_allocate_function");
  auto func_obj = builder.CreateCall(allocFn, {nameStr, arityConst, funcPtr});

  // Allocate closure with upvalue slots
  auto alloc_closure_fn = mod.getFunction("elx_allocate_closure");
  auto upvalue_count =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), upvalues.size());
  auto closure_obj =
      builder.CreateCall(alloc_closure_fn, {func_obj, upvalue_count});

  // Set each upvalue using pre-captured values
  for (int i = 0; i < static_cast<int>(upvalues.size()); i++) {
    llvm::Value *upvalue_value;

    // Use pre-captured upvalue if available
    auto it = capturedUpvalues.find(upvalues[i]);
    if (it != capturedUpvalues.end()) {
      upvalue_value =
          it->second; // This is already an upvalue object from captureUpvalue
    } else {
      // Fallback - this shouldn't happen if called correctly
      upvalue_value = captureUpvalue(upvalues[i]);
    }

    auto set_upvalue_fn = mod.getFunction("elx_set_closure_upvalue");
    auto index = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), i);
    builder.CreateCall(set_upvalue_fn, {closure_obj, index, upvalue_value});
  }

  return closure_obj;
}

llvm::Value *CodeGenVisitor::createDeferredClosure(
    llvm::Function *func, const std::vector<std::string> &upvalues, int arity,
    const std::string &funcName) {
  // Create the function object using the original Lox arity
  // The upvalue array parameter is an implementation detail
  auto nameStr = builder.CreateGlobalStringPtr(funcName, "fname");
  auto arityConst = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), arity);
  auto funcPtr = builder.CreateBitCast(
      func, llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0));

  auto allocFn = mod.getFunction("elx_allocate_function");
  // Fix parameter order: name, arity, function_pointer
  auto func_obj = builder.CreateCall(allocFn, {nameStr, arityConst, funcPtr});

  // Allocate closure with upvalue slots
  auto alloc_closure_fn = mod.getFunction("elx_allocate_closure");
  auto upvalue_count =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), upvalues.size());
  auto closure_obj =
      builder.CreateCall(alloc_closure_fn, {func_obj, upvalue_count});

  // Capture each upvalue
  for (int i = 0; i < static_cast<int>(upvalues.size()); i++) {
    auto upvalue_value = captureUpvalue(upvalues[i]);
    auto set_upvalue_fn = mod.getFunction("elx_set_closure_upvalue");
    auto index = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), i);
    builder.CreateCall(set_upvalue_fn, {closure_obj, index, upvalue_value});
  }

  return closure_obj;
}

llvm::Value *CodeGenVisitor::accessUpvalue(const std::string &name, int index) {
  if (function_stack.empty()) {
    return nilConst();
  }

  const FunctionContext &current_ctx = function_stack.top();
  if (!current_ctx.upvalue_array) {
    return nilConst();
  }

  // The upvalue array contains upvalue objects. Load the object then ask the
  // runtime for the current value so multiple closures stay in sync.
  auto index_val = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), index);
  auto upvalue_ptr =
      builder.CreateGEP(llvmValueTy(), current_ctx.upvalue_array, index_val);
  auto upvalue_value = builder.CreateLoad(llvmValueTy(), upvalue_ptr);

  auto get_upvalue_fn = mod.getFunction("elx_get_upvalue_value");
  if (!get_upvalue_fn) {
    return upvalue_value;
  }

  return builder.CreateCall(get_upvalue_fn, {upvalue_value}, name + "_value");
}

llvm::Value *CodeGenVisitor::captureUpvalue(const std::string &name) {
  auto alloc_upvalue_fn = mod.getFunction("elx_allocate_upvalue");
  if (!alloc_upvalue_fn) {
    return nilConst();
  }

  llvm::Value *slot = nullptr;

  auto stackIt = variableStacks.find(name);
  if (stackIt != variableStacks.end() && !stackIt->second.empty()) {
    slot = stackIt->second.back();
  }

  if (!slot) {
    auto currentIt = locals.find(name + "_current");
    if (currentIt != locals.end()) {
      slot = currentIt->second;
    } else {
      auto it = locals.find(name);
      if (it != locals.end()) {
        slot = it->second;
      }
    }
  }

  if (slot && directValues.count(name)) {
    // Convert direct values (like parameters) into stack storage so they can be
    // captured
    auto directValue = slot;
    auto fn = builder.GetInsertBlock()->getParent();
    std::string slotName = name + "_captured" + std::to_string(variableCounter++);
    auto storage = createStackAlloca(fn, llvmValueTy(), slotName);
    builder.CreateStore(directValue, storage);
    slot = storage;

    locals[name] = slot;
    locals[name + "_current"] = slot;
    directValues.erase(name);

    if (!variableStacks[name].empty()) {
      variableStacks[name].back() = slot;
    } else {
      variableStacks[name].push_back(slot);
    }

    if (!function_stack.empty()) {
      FunctionContext &ctx = function_stack.top();
      auto it = std::find(ctx.local_slots.begin(), ctx.local_slots.end(), directValue);
      if (it != ctx.local_slots.end()) {
        *it = slot;
      } else {
        if (ctx.local_slots.size() >=
            static_cast<size_t>(MAX_USER_LOCAL_SLOTS)) {
          throw CompileError("Too many local variables in function.");
        }
        ctx.local_slots.push_back(slot);
      }
      ctx.localCount = static_cast<int>(ctx.local_slots.size());
    } else {
      global_local_slots.push_back(slot);
    }
  }

  if (slot) {
    if (!globalVariables.count(name)) {
      if (!function_stack.empty()) {
        FunctionContext &ctx = function_stack.top();
        ctx.captured_slots.insert(slot);
        if (std::find(ctx.local_slots.begin(), ctx.local_slots.end(), slot) ==
            ctx.local_slots.end()) {
          if (ctx.local_slots.size() >=
              static_cast<size_t>(MAX_USER_LOCAL_SLOTS)) {
            throw CompileError("Too many local variables in function.");
          }
          ctx.local_slots.push_back(slot);
        }
        ctx.localCount = static_cast<int>(ctx.local_slots.size());
      } else {
        global_captured_slots.insert(slot);
        if (std::find(global_local_slots.begin(), global_local_slots.end(), slot) ==
            global_local_slots.end()) {
          global_local_slots.push_back(slot);
        }
      }
    }

    return builder.CreateCall(alloc_upvalue_fn, {slot});
  }

  if (!function_stack.empty()) {
    const FunctionContext &current_ctx = function_stack.top();
    auto upvalue_it = current_ctx.upvalue_indices.find(name);
    if (upvalue_it != current_ctx.upvalue_indices.end() &&
        current_ctx.upvalue_array) {
      auto index_val =
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), upvalue_it->second);
      auto upvalue_ptr = builder.CreateGEP(llvmValueTy(), current_ctx.upvalue_array,
                                           index_val);
      return builder.CreateLoad(llvmValueTy(), upvalue_ptr);
    }
  }

  // Variable not found - allocate independent storage initialized to nil
  llvm::Function *mallocFn = mod.getFunction("malloc");
  if (!mallocFn) {
    llvm::FunctionType *mallocType = llvm::FunctionType::get(
        llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0),
        {llvm::Type::getInt64Ty(ctx)}, false);
    mallocFn = llvm::Function::Create(
        mallocType, llvm::Function::ExternalLinkage, "malloc", &mod);
  }

  auto size = llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx), sizeof(uint64_t));
  auto heap_ptr = builder.CreateCall(mallocFn, {size});
  auto value_ptr =
      builder.CreateBitCast(heap_ptr, llvm::PointerType::get(llvmValueTy(), 0));
  builder.CreateStore(nilConst(), value_ptr);

  return builder.CreateCall(alloc_upvalue_fn, {value_ptr});
}

} // namespace eloxir
