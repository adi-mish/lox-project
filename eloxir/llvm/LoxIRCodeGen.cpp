#include "LoxIRCodeGen.h"

#include "BuiltinsIR.h"
#include "../runtime/RuntimeAPI.h"
#include "../runtime/Value.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

namespace eloxir::loxir {
namespace {

static constexpr uint64_t kQnan = 0x7ff8000000000000ULL;
static constexpr int kPropertyCallMethod = 2;

uint64_t numberBits(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

uint64_t boolBits(bool value) {
  return kQnan | (static_cast<uint64_t>(Tag::BOOL) << 48) |
         (value ? 1ULL : 0ULL);
}

uint64_t nilBits() {
  return kQnan | (static_cast<uint64_t>(Tag::NIL) << 48);
}

class Emitter {
public:
  explicit Emitter(llvm::Module &module)
      : module_(module), ctx_(module.getContext()), builder_(ctx_) {
    declareRuntimeBuiltins(module_);
  }

  std::optional<std::string> emit(const LoxModule &loxModule) {
    if (loxModule.functions().empty()) {
      return "module does not contain any functions";
    }

    for (const LoxFunction &function : loxModule.functions()) {
      declareFunction(function);
    }
    for (const LoxFunction &function : loxModule.functions()) {
      if (auto failure = emitFunction(function)) {
        return failure;
      }
    }
    return std::nullopt;
  }

private:
  llvm::Module &module_;
  llvm::LLVMContext &ctx_;
  llvm::IRBuilder<> builder_;
  llvm::Function *function_ = nullptr;
  const LoxFunction *loxFunction_ = nullptr;
  llvm::Value *upvalueArray_ = nullptr;
  std::unordered_map<uint32_t, llvm::BasicBlock *> blocks_;
  std::unordered_map<uint32_t, llvm::Value *> values_;
  std::unordered_map<uint32_t, LoxType> types_;
  std::unordered_map<std::string, llvm::Value *> locals_;
  std::unordered_set<std::string> capturedLocals_;
  std::unordered_map<std::string, llvm::Function *> functions_;
  std::unordered_map<std::string, const LoxFunction *> loxFunctions_;
  std::unordered_map<std::string, llvm::GlobalVariable *> internedStrings_;
  std::unordered_map<uint32_t, llvm::Value *> preparedCallCaches_;
  struct KnownClass {
    std::string name;
    bool hasSuperclass = false;
    std::string initializerFunction;
  };
  std::unordered_set<std::string> reassignedLocals_;
  std::unordered_map<uint32_t, std::string> functionValueTargets_;
  std::unordered_map<uint32_t, std::string> classValueTargets_;
  std::unordered_map<std::string, std::string> localClassTargets_;
  std::unordered_map<std::string, KnownClass> knownClasses_;

  llvm::IntegerType *valueTy() { return llvm::Type::getInt64Ty(ctx_); }
  llvm::IntegerType *i8Ty() { return llvm::Type::getInt8Ty(ctx_); }
  llvm::IntegerType *i32Ty() { return llvm::Type::getInt32Ty(ctx_); }
  llvm::IntegerType *i64Ty() { return llvm::Type::getInt64Ty(ctx_); }
  llvm::PointerType *i8PtrTy() {
    return llvm::PointerType::get(llvm::Type::getInt8Ty(ctx_), 0);
  }
  llvm::PointerType *valuePtrTy() {
    return llvm::PointerType::get(valueTy(), 0);
  }

  llvm::PointerType *propertyCachePtrTy() {
    return llvm::PointerType::get(getOrCreatePropertyCacheIRType(ctx_), 0);
  }

  llvm::PointerType *callCachePtrTy() {
    return llvm::PointerType::get(getOrCreateCallInlineCacheIRType(ctx_), 0);
  }

  llvm::ConstantInt *constantValue(uint64_t bits) {
    return llvm::ConstantInt::get(valueTy(), bits);
  }

  llvm::ConstantInt *constantI32(int value) {
    return llvm::ConstantInt::get(i32Ty(), static_cast<uint32_t>(value));
  }

  llvm::Value *nilValue() { return constantValue(nilBits()); }

  llvm::Value *boolValue(llvm::Value *condition) {
    return builder_.CreateSelect(condition, constantValue(boolBits(true)),
                                 constantValue(boolBits(false)), "loxbool");
  }

  llvm::Value *asDouble(llvm::Value *value) {
    return builder_.CreateBitCast(value, llvm::Type::getDoubleTy(ctx_),
                                  "as.double");
  }

  llvm::Value *fromDouble(llvm::Value *value) {
    return builder_.CreateBitCast(value, valueTy(), "as.value");
  }

  llvm::Value *truthy(llvm::Value *value) {
    auto *isNil = builder_.CreateICmpEQ(value, constantValue(nilBits()),
                                        "is.nil");
    auto *isFalse = builder_.CreateICmpEQ(value, constantValue(boolBits(false)),
                                          "is.false");
    return builder_.CreateNot(builder_.CreateOr(isNil, isFalse), "truthy");
  }

  llvm::Value *truthy(ValueId id) {
    LoxType type = typeOf(id);
    switch (type) {
    case LoxType::Nil:
      return builder_.getFalse();
    case LoxType::Bool:
      return builder_.CreateICmpNE(lookup(id), constantValue(boolBits(false)),
                                   "truthy.bool");
    case LoxType::Number:
    case LoxType::String:
    case LoxType::Object:
    case LoxType::Function:
    case LoxType::Closure:
    case LoxType::Class:
    case LoxType::Instance:
      return builder_.getTrue();
    case LoxType::Unknown:
      return truthy(lookup(id));
    }
    return truthy(lookup(id));
  }

  llvm::Function *runtime(const char *name) {
    return module_.getFunction(name);
  }

  llvm::Value *createStackSlot(const std::string &name) {
    llvm::IRBuilder<> entryBuilder(
        &function_->getEntryBlock(), function_->getEntryBlock().begin());
    return entryBuilder.CreateAlloca(valueTy(), nullptr, name + ".slot");
  }

  llvm::Value *createStackArray(size_t count, const std::string &name) {
    llvm::IRBuilder<> entryBuilder(
        &function_->getEntryBlock(), function_->getEntryBlock().begin());
    auto *arraySize =
        llvm::ConstantInt::get(i32Ty(), static_cast<uint32_t>(count));
    return entryBuilder.CreateAlloca(valueTy(), arraySize, name + ".storage");
  }

  llvm::Value *createCallCache(const Instruction &instruction) {
    auto *cacheTy = getOrCreateCallInlineCacheIRType(ctx_);
    std::string name = "elx.call.cache";
    if (instruction.result) {
      name += "." + std::to_string(instruction.result->id);
    }
    return new llvm::GlobalVariable(
        module_, cacheTy, false, llvm::GlobalValue::PrivateLinkage,
        llvm::Constant::getNullValue(cacheTy), name);
  }

  llvm::Value *createPropertyCache(const Instruction &instruction) {
    auto *cacheTy = getOrCreatePropertyCacheIRType(ctx_);
    std::string name = "elx.property.cache";
    if (instruction.result) {
      name += "." + std::to_string(instruction.result->id);
    }
    return new llvm::GlobalVariable(
        module_, cacheTy, false, llvm::GlobalValue::PrivateLinkage,
        llvm::Constant::getNullValue(cacheTy), name);
  }

  llvm::Value *createHeapSlot(llvm::Value *initial,
                              const std::string &name) {
    auto *allocate = runtime("elx_allocate_value_slot");
    if (!allocate) {
      return nullptr;
    }
    auto *slot =
        builder_.CreateCall(allocate, {initial}, name + ".heap.slot");
    guardRuntimeError();
    return slot;
  }

  bool isCapturedLocal(const std::string &name) const {
    return capturedLocals_.find(name) != capturedLocals_.end();
  }

  llvm::Value *localSlot(const std::string &name) {
    auto it = locals_.find(name);
    if (it != locals_.end()) {
      return it->second;
    }

    llvm::Value *slot = nullptr;
    if (isCapturedLocal(name)) {
      llvm::IRBuilder<> entryBuilder(
          &function_->getEntryBlock(), function_->getEntryBlock().begin());
      auto *allocate = runtime("elx_allocate_value_slot");
      if (!allocate) {
        return nullptr;
      }
      slot =
          entryBuilder.CreateCall(allocate, {nilValue()}, name + ".heap.slot");
    } else {
      slot = createStackSlot(name);
    }
    locals_[name] = slot;
    return slot;
  }

  llvm::Value *stringPtr(const std::string &text, const std::string &name) {
    return builder_.CreateGlobalStringPtr(text, name);
  }

  llvm::GlobalVariable *internedStringSlot(const std::string &text) {
    auto it = internedStrings_.find(text);
    if (it != internedStrings_.end()) {
      return it->second;
    }

    auto *slot = new llvm::GlobalVariable(
        module_, valueTy(), false, llvm::GlobalValue::PrivateLinkage,
        constantValue(0), "elx.intern.cache." +
                              std::to_string(internedStrings_.size()));
    internedStrings_[text] = slot;
    return slot;
  }

  llvm::Value *cachedInternedString(const std::string &text,
                                    const std::string &name) {
    auto *intern = runtime("elx_intern_string");
    if (!intern) {
      return nilValue();
    }

    auto *slot = internedStringSlot(text);
    auto *cached = builder_.CreateLoad(valueTy(), slot, name + ".cached");
    auto *hasCached = builder_.CreateICmpNE(cached, constantValue(0),
                                            name + ".is.cached");
    auto *hitBlock = builder_.GetInsertBlock();
    auto *missBlock =
        llvm::BasicBlock::Create(ctx_, name + ".intern", function_);
    auto *readyBlock =
        llvm::BasicBlock::Create(ctx_, name + ".ready", function_);
    builder_.CreateCondBr(hasCached, readyBlock, missBlock);

    builder_.SetInsertPoint(missBlock);
    auto *fresh = builder_.CreateCall(
        intern, {stringPtr(text, name),
                 constantI32(static_cast<int>(text.size()))},
        name + ".interned.fresh");
    builder_.CreateStore(fresh, slot);
    auto *missEnd = builder_.GetInsertBlock();
    builder_.CreateBr(readyBlock);

    builder_.SetInsertPoint(readyBlock);
    auto *result = builder_.CreatePHI(valueTy(), 2, name + ".interned");
    result->addIncoming(cached, hitBlock);
    result->addIncoming(fresh, missEnd);
    return result;
  }

  llvm::Value *nullValuePtr() {
    return llvm::ConstantPointerNull::get(valuePtrTy());
  }

  llvm::Value *nullI8Ptr() {
    return llvm::ConstantPointerNull::get(i8PtrTy());
  }

  llvm::Value *constantI64(uint64_t value) {
    return llvm::ConstantInt::get(i64Ty(), value);
  }

  llvm::Value *runtimeIntAddress(const int *pointer) {
    auto address =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
    return llvm::ConstantExpr::getIntToPtr(
        llvm::ConstantInt::get(i64Ty(), address),
        llvm::PointerType::get(i32Ty(), 0));
  }

  llvm::Value *runtimeErrorFlagAddress() {
    return runtimeIntAddress(&elx_runtime_error_flag);
  }

  llvm::Value *currentCallDepthAddress() {
    return runtimeIntAddress(&elx_current_call_depth);
  }

  llvm::Value *isObjectValue(llvm::Value *value) {
    auto *isTagged = builder_.CreateICmpEQ(
        builder_.CreateAnd(value, constantI64(0xfff8000000000000ULL)),
        constantI64(kQnan), "is.tagged");
    auto *tag = builder_.CreateAnd(
        builder_.CreateLShr(value, constantI64(48)), constantI64(0x7),
        "value.tag");
    auto *isObject =
        builder_.CreateICmpEQ(tag,
                              constantI64(static_cast<uint64_t>(Tag::OBJ)),
                              "is.object.tag");
    return builder_.CreateAnd(isTagged, isObject, "is.object");
  }

  llvm::Value *isNumberValue(llvm::Value *value) {
    auto *isTagged = builder_.CreateICmpEQ(
        builder_.CreateAnd(value, constantI64(0xfff8000000000000ULL)),
        constantI64(kQnan), "is.tagged");
    auto *tag = builder_.CreateAnd(
        builder_.CreateLShr(value, constantI64(48)), constantI64(0x7),
        "value.tag");
    auto *isNumberTag =
        builder_.CreateICmpEQ(tag,
                              constantI64(static_cast<uint64_t>(Tag::NUMBER)),
                              "is.number.tag");
    return builder_.CreateOr(builder_.CreateNot(isTagged), isNumberTag,
                             "is.number");
  }

  llvm::Value *objectPointer(llvm::Value *value,
                             const std::string &name) {
    auto *raw =
        builder_.CreateAnd(value, constantI64(0x0000ffffffffffffULL),
                           name + ".raw");
    return builder_.CreateIntToPtr(raw, i8PtrTy(), name);
  }

  llvm::Value *fieldAddress(llvm::Value *base, size_t offset,
                            const std::string &name) {
    return builder_.CreateGEP(i8Ty(), base, constantI64(offset), name);
  }

  llvm::Value *loadI32AtOffset(llvm::Value *base, size_t offset,
                               const std::string &name) {
    return builder_.CreateLoad(i32Ty(), fieldAddress(base, offset, name + ".addr"),
                               name);
  }

  llvm::Value *loadI64AtOffset(llvm::Value *base, size_t offset,
                               const std::string &name) {
    return builder_.CreateLoad(i64Ty(), fieldAddress(base, offset, name + ".addr"),
                               name);
  }

  llvm::Value *loadPointerAtOffset(llvm::Value *base, size_t offset,
                                   const std::string &name) {
    return builder_.CreateLoad(i8PtrTy(),
                               fieldAddress(base, offset, name + ".addr"),
                               name);
  }

  llvm::Value *cacheEntryShape(llvm::Value *cache, uint32_t index) {
    auto *cacheTy = getOrCreatePropertyCacheIRType(ctx_);
    auto *entryPtr = builder_.CreateStructGEP(
        cacheTy, cache, 1, "property.cache.entries");
    auto *entry = builder_.CreateGEP(
        llvm::ArrayType::get(getOrCreatePropertyCacheEntryIRType(ctx_),
                             eloxir::PROPERTY_CACHE_MAX_SIZE),
        entryPtr, {constantI64(0), constantI64(index)},
        "property.cache.entry");
    return builder_.CreateLoad(
        i8PtrTy(),
        builder_.CreateStructGEP(getOrCreatePropertyCacheEntryIRType(ctx_),
                                 entry, 0, "property.cache.shape.addr"),
        "property.cache.shape");
  }

  llvm::Value *cacheEntrySlot(llvm::Value *cache, uint32_t index) {
    auto *cacheTy = getOrCreatePropertyCacheIRType(ctx_);
    auto *entryPtr = builder_.CreateStructGEP(
        cacheTy, cache, 1, "property.cache.entries");
    auto *entry = builder_.CreateGEP(
        llvm::ArrayType::get(getOrCreatePropertyCacheEntryIRType(ctx_),
                             eloxir::PROPERTY_CACHE_MAX_SIZE),
        entryPtr, {constantI64(0), constantI64(index)},
        "property.cache.entry");
    auto *slot32 = builder_.CreateLoad(
        i32Ty(),
        builder_.CreateStructGEP(getOrCreatePropertyCacheEntryIRType(ctx_),
                                 entry, 1, "property.cache.slot.addr"),
        "property.cache.slot");
    return builder_.CreateZExt(slot32, i64Ty(), "property.cache.slot64");
  }

  llvm::Value *cacheHasEntry(llvm::Value *cache, uint32_t index) {
    auto *cacheTy = getOrCreatePropertyCacheIRType(ctx_);
    auto *size = builder_.CreateLoad(
        i32Ty(),
        builder_.CreateStructGEP(cacheTy, cache, 0, "property.cache.size.addr"),
        "property.cache.size");
    return builder_.CreateICmpUGT(
        size, llvm::ConstantInt::get(i32Ty(), index), "property.cache.has");
  }

  llvm::Value *loadCallCacheI64(llvm::Value *cache, unsigned fieldIndex,
                                const std::string &name) {
    return builder_.CreateLoad(
        i64Ty(),
        builder_.CreateStructGEP(getOrCreateCallInlineCacheIRType(ctx_), cache,
                                 fieldIndex, name + ".addr"),
        name);
  }

  llvm::Value *loadCallCacheI32(llvm::Value *cache, unsigned fieldIndex,
                                const std::string &name) {
    return builder_.CreateLoad(
        i32Ty(),
        builder_.CreateStructGEP(getOrCreateCallInlineCacheIRType(ctx_), cache,
                                 fieldIndex, name + ".addr"),
        name);
  }

  llvm::Value *loadCallCachePtr(llvm::Value *cache, unsigned fieldIndex,
                                const std::string &name) {
    return builder_.CreateLoad(
        i8PtrTy(),
        builder_.CreateStructGEP(getOrCreateCallInlineCacheIRType(ctx_), cache,
                                 fieldIndex, name + ".addr"),
        name);
  }

  void guardRuntimeError() {
    if (!function_) {
      return;
    }

    auto *current = builder_.GetInsertBlock();
    if (!current || current->getTerminator()) {
      return;
    }

    auto *errorBlock =
        llvm::BasicBlock::Create(ctx_, "runtime.error", function_);
    auto *continueBlock =
        llvm::BasicBlock::Create(ctx_, "runtime.cont", function_);
    auto *errored = builder_.CreateICmpNE(
        builder_.CreateLoad(i32Ty(), runtimeErrorFlagAddress(),
                            "runtime.error.flag"),
        constantI32(0),
        "has.runtime.error.bool");
    builder_.CreateCondBr(errored, errorBlock, continueBlock);

    builder_.SetInsertPoint(errorBlock);
    builder_.CreateRet(nilValue());

    builder_.SetInsertPoint(continueBlock);
  }

  void emitStackOverflowReturn() {
    if (auto *runtimeError = runtime("elx_runtime_error")) {
      builder_.CreateCall(runtimeError,
                          {stringPtr("Stack overflow.",
                                     "stack.overflow.message")});
    }
    builder_.CreateRet(nilValue());
  }

  void emitEnterCallFrame(llvm::BasicBlock *callBlock,
                          llvm::BasicBlock *overflowBlock,
                          const std::string &name) {
    auto *depthPtr = currentCallDepthAddress();
    auto *depth = builder_.CreateLoad(i32Ty(), depthPtr, name + ".depth");
    auto *canEnter =
        builder_.CreateICmpSLT(depth, constantI32(256), name + ".can.enter");
    builder_.CreateCondBr(canEnter, callBlock, overflowBlock);

    builder_.SetInsertPoint(callBlock);
    builder_.CreateStore(
        builder_.CreateAdd(depth, constantI32(1), name + ".depth.next"),
        depthPtr);
  }

  void emitLeaveCallFrame(const std::string &name) {
    auto *depthPtr = currentCallDepthAddress();
    auto *depth = builder_.CreateLoad(i32Ty(), depthPtr, name + ".depth");
    auto *positive =
        builder_.CreateICmpSGT(depth, constantI32(0), name + ".depth.positive");
    auto *previous =
        builder_.CreateSub(depth, constantI32(1), name + ".depth.prev");
    auto *next = builder_.CreateSelect(positive, previous, constantI32(0),
                                       name + ".depth.after.leave");
    builder_.CreateStore(next, depthPtr);
  }

  llvm::Value *emitArgumentArray(const std::vector<ValueId> &arguments,
                                 const std::string &name) {
    if (arguments.empty()) {
      return nullValuePtr();
    }

    auto *storage = createStackArray(arguments.size(), name);
    for (size_t index = 0; index < arguments.size(); ++index) {
      auto *slot = builder_.CreateGEP(
          valueTy(), storage,
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), index),
          name + ".slot");
      builder_.CreateStore(lookup(arguments[index]), slot);
    }
    return storage;
  }

  llvm::Function *declareFunction(const LoxFunction &function) {
    std::vector<llvm::Type *> parameterTypes(function.parameters().size(),
                                             valueTy());
    if (!function.upvalues().empty()) {
      parameterTypes.push_back(valuePtrTy());
    }
    auto *fnTy = llvm::FunctionType::get(valueTy(), parameterTypes, false);
    auto *llvmFunction = llvm::Function::Create(
        fnTy, llvm::Function::ExternalLinkage, function.name(), module_);
    functions_[function.name()] = llvmFunction;
    loxFunctions_[function.name()] = &function;
    return llvmFunction;
  }

  std::optional<std::string> emitFunction(const LoxFunction &function) {
    function_ = functions_.at(function.name());
    loxFunction_ = &function;
    upvalueArray_ = nullptr;
    blocks_.clear();
    values_.clear();
    types_.clear();
    locals_.clear();
    preparedCallCaches_.clear();
    functionValueTargets_.clear();
    classValueTargets_.clear();
    localClassTargets_.clear();
    knownClasses_.clear();
    capturedLocals_ = capturedLocalSymbols(function);
    reassignedLocals_ = reassignedLocalSymbols(function);

    for (const auto &block : function.blocks()) {
      blocks_[block.id().id] =
          llvm::BasicBlock::Create(ctx_, block.name(), function_);
    }

    builder_.SetInsertPoint(blocks_.at(function.blocks().front().id().id));
    auto argument = function_->arg_begin();
    for (const auto &parameter : function.parameters()) {
      if (argument == function_->arg_end()) {
        return "function @" + function.name() + " has too few LLVM arguments";
      }
      argument->setName(parameter.name);
      auto *slot = localSlot(parameter.name);
      if (!slot) {
        return "missing elx_allocate_value_slot";
      }
      builder_.CreateStore(&*argument, slot);
      bindParameter(parameter, &*argument);
      ++argument;
    }
    if (!function.upvalues().empty()) {
      if (argument == function_->arg_end()) {
        return "function @" + function.name() + " missing upvalue array";
      }
      argument->setName("upvalues");
      upvalueArray_ = &*argument;
    }

    for (const auto &block : function.blocks()) {
      builder_.SetInsertPoint(blocks_.at(block.id().id));
      for (const auto &instruction : block.instructions()) {
        if (auto failure = emitInstruction(instruction)) {
          return failure;
        }
      }
      if (!builder_.GetInsertBlock()->getTerminator()) {
        builder_.CreateRet(nilValue());
      }
    }

    return std::nullopt;
  }

  void bindParameter(const Parameter &parameter, llvm::Value *value) {
    values_[parameter.value.id] = value;
    types_[parameter.value.id] = parameter.type;
  }

  llvm::Value *lookup(ValueId id) const {
    auto it = values_.find(id.id);
    return it == values_.end() ? nullptr : it->second;
  }

  LoxType typeOf(ValueId id) const {
    auto it = types_.find(id.id);
    return it == types_.end() ? LoxType::Unknown : it->second;
  }

  std::unordered_set<std::string>
  capturedLocalSymbols(const LoxFunction &) const {
    std::unordered_set<std::string> symbols;
    for (const auto &entry : loxFunctions_) {
      for (const Upvalue &upvalue : entry.second->upvalues()) {
        if (upvalue.source == UpvalueSourceKind::Local &&
            !upvalue.sourceSymbol.empty()) {
          symbols.insert(upvalue.sourceSymbol);
        }
      }
    }
    return symbols;
  }

  std::unordered_set<std::string>
  reassignedLocalSymbols(const LoxFunction &function) const {
    std::unordered_set<std::string> symbols;
    for (const auto &block : function.blocks()) {
      for (const auto &instruction : block.instructions()) {
        if (instruction.kind == InstructionKind::StoreLocal &&
            !instruction.declaresSymbol && !instruction.symbol.empty()) {
          symbols.insert(instruction.symbol);
        }
      }
    }
    return symbols;
  }

  bool isLeafFunction(const LoxFunction &function) const {
    for (const auto &block : function.blocks()) {
      for (const auto &instruction : block.instructions()) {
        switch (instruction.kind) {
        case InstructionKind::Call:
        case InstructionKind::DirectCall:
        case InstructionKind::CallPreparedProperty:
          return false;
        case InstructionKind::BuiltinCall:
          break;
        default:
          break;
        }
      }
    }
    return true;
  }

  bool functionCannotRaiseRuntimeError(const LoxFunction &function) const {
    for (const auto &block : function.blocks()) {
      for (const auto &instruction : block.instructions()) {
        switch (instruction.kind) {
        case InstructionKind::ConstantNil:
        case InstructionKind::ConstantBool:
        case InstructionKind::ConstantNumber:
        case InstructionKind::ConstantString:
        case InstructionKind::LoadLocal:
        case InstructionKind::StoreLocal:
        case InstructionKind::IsTruthy:
        case InstructionKind::Print:
        case InstructionKind::BuiltinCall:
        case InstructionKind::Jump:
        case InstructionKind::Branch:
        case InstructionKind::Return:
        case InstructionKind::Unreachable:
          break;
        case InstructionKind::Unary:
          if (instruction.unaryOp == UnaryOp::Not) {
            break;
          }
          return false;
        case InstructionKind::Binary:
          if (instruction.binaryOp == BinaryOp::Equal ||
              instruction.binaryOp == BinaryOp::NotEqual) {
            break;
          }
          return false;
        default:
          return false;
        }
      }
    }
    return true;
  }

  uint32_t functionRuntimeFlags(const LoxFunction &function) const {
    uint32_t flags = 0;
    if (isLeafFunction(function)) {
      flags |= FUNCTION_FLAG_LEAF;
    }
    if (functionCannotRaiseRuntimeError(function)) {
      flags |= FUNCTION_FLAG_NO_RUNTIME_ERROR;
    }
    return flags;
  }

  void bind(const Instruction &instruction, llvm::Value *value,
            LoxType type = LoxType::Unknown) {
    if (!instruction.result) {
      return;
    }
    values_[instruction.result->id] = value;
    types_[instruction.result->id] = type;
  }

  void bindValue(ValueId id, llvm::Value *value,
                 LoxType type = LoxType::Unknown) {
    values_[id.id] = value;
    types_[id.id] = type;
  }

  std::optional<std::string> classTarget(ValueId id) const {
    auto it = classValueTargets_.find(id.id);
    if (it == classValueTargets_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::optional<std::string> functionTarget(ValueId id) const {
    auto it = functionValueTargets_.find(id.id);
    if (it == functionValueTargets_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::optional<std::string> unsupported(const Instruction &instruction,
                                         const char *reason) const {
    std::ostringstream out;
    out << "unsupported " << toString(instruction.kind);
    if (!instruction.symbol.empty()) {
      out << " @" << instruction.symbol;
    }
    if (instruction.source.line > 0) {
      out << " on line " << instruction.source.line;
    }
    out << ": " << reason;
    return out.str();
  }

  std::optional<std::string> requireOperands(const Instruction &instruction,
                                             size_t count) const {
    if (instruction.operands.size() != count) {
      return unsupported(instruction, "unexpected operand count");
    }
    for (ValueId id : instruction.operands) {
      if (!lookup(id)) {
        return unsupported(instruction, "operand was not defined");
      }
    }
    return std::nullopt;
  }

  std::optional<std::string> emitInstruction(const Instruction &instruction) {
    switch (instruction.kind) {
    case InstructionKind::ConstantNil:
      bind(instruction, nilValue(), LoxType::Nil);
      return std::nullopt;
    case InstructionKind::ConstantBool:
      bind(instruction, constantValue(boolBits(instruction.boolValue)),
           LoxType::Bool);
      return std::nullopt;
    case InstructionKind::ConstantNumber:
      bind(instruction, constantValue(numberBits(instruction.numberValue)),
           LoxType::Number);
      return std::nullopt;
    case InstructionKind::ConstantString:
      return emitString(instruction);
    case InstructionKind::LoadGlobal:
      return emitLoadGlobal(instruction);
    case InstructionKind::StoreGlobal:
      return emitStoreGlobal(instruction);
    case InstructionKind::LoadLocal:
      return emitLoadLocal(instruction);
    case InstructionKind::StoreLocal:
      return emitStoreLocal(instruction);
    case InstructionKind::LoadUpvalue:
      return emitLoadUpvalue(instruction);
    case InstructionKind::StoreUpvalue:
      return emitStoreUpvalue(instruction);
    case InstructionKind::Binary:
      return emitBinary(instruction);
    case InstructionKind::Unary:
      return emitUnary(instruction);
    case InstructionKind::IsTruthy:
      return emitIsTruthy(instruction);
    case InstructionKind::Print:
      return emitPrint(instruction);
    case InstructionKind::Jump:
      return emitJump(instruction);
    case InstructionKind::Branch:
      return emitBranch(instruction);
    case InstructionKind::Return:
      return emitReturn(instruction);
    case InstructionKind::Call:
      return emitCall(instruction);
    case InstructionKind::DirectCall:
      return emitDirectCall(instruction);
    case InstructionKind::BuiltinCall:
      return emitBuiltinCall(instruction);
    case InstructionKind::PreparePropertyCall:
      return emitPreparePropertyCall(instruction);
    case InstructionKind::CallPreparedProperty:
      return emitCallPreparedProperty(instruction);
    case InstructionKind::GetProperty:
      return emitGetProperty(instruction);
    case InstructionKind::SetProperty:
      return emitSetProperty(instruction);
    case InstructionKind::DefineClass:
      return emitDefineClass(instruction);
    case InstructionKind::DefineMethod:
      return emitDefineMethod(instruction);
    case InstructionKind::BindSuper:
      return emitBindSuper(instruction);
    case InstructionKind::DefineFunction:
      return emitDefineFunction(instruction);
    case InstructionKind::Phi:
    case InstructionKind::CloseUpvalues:
      return unsupported(instruction, "not in the generic backend subset");
    case InstructionKind::Unreachable:
      builder_.CreateUnreachable();
      return std::nullopt;
    }
    return unsupported(instruction, "unknown instruction");
  }

  std::optional<std::string> emitString(const Instruction &instruction) {
    if (!runtime("elx_intern_string")) {
      return unsupported(instruction, "missing elx_intern_string");
    }
    auto *value = cachedInternedString(instruction.symbol, "lox.str");
    bind(instruction, value, LoxType::String);
    return std::nullopt;
  }

  llvm::Value *emitInternedName(const std::string &text,
                                const std::string &name) {
    return cachedInternedString(text, name);
  }

  std::optional<std::string> emitLoadGlobal(const Instruction &instruction) {
    auto *hasVariable = runtime("elx_has_global_variable");
    auto *loadVariable = runtime("elx_get_global_variable");
    auto *hasFunction = runtime("elx_has_global_function");
    auto *loadFunction = runtime("elx_get_global_function");
    auto *loadBuiltin = runtime("elx_get_global_builtin");
    auto *runtimeError = runtime("elx_runtime_error");
    if (!hasVariable || !loadVariable || !hasFunction || !loadFunction ||
        !loadBuiltin || !runtimeError) {
      return unsupported(instruction, "missing global runtime helper");
    }
    auto *name = stringPtr(instruction.symbol, "global.name");
    auto *hasVar = builder_.CreateICmpNE(
        builder_.CreateCall(hasVariable, {name}, "has.global.var"),
        constantI32(0), "has.global.var.bool");

    auto *varBlock = llvm::BasicBlock::Create(ctx_, "global.var", function_);
    auto *functionCheckBlock =
        llvm::BasicBlock::Create(ctx_, "global.func.check", function_);
    auto *functionBlock =
        llvm::BasicBlock::Create(ctx_, "global.func", function_);
    auto *builtinBlock =
        llvm::BasicBlock::Create(ctx_, "global.builtin", function_);
    auto *missingBlock =
        llvm::BasicBlock::Create(ctx_, "global.missing", function_);
    auto *doneBlock = llvm::BasicBlock::Create(ctx_, "global.done", function_);
    builder_.CreateCondBr(hasVar, varBlock, functionCheckBlock);

    builder_.SetInsertPoint(varBlock);
    auto *varValue = builder_.CreateCall(loadVariable, {name}, "global.var");
    builder_.CreateBr(doneBlock);
    varBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(functionCheckBlock);
    auto *hasFunc = builder_.CreateICmpNE(
        builder_.CreateCall(hasFunction, {name}, "has.global.func"),
        constantI32(0), "has.global.func.bool");
    builder_.CreateCondBr(hasFunc, functionBlock, builtinBlock);

    builder_.SetInsertPoint(functionBlock);
    auto *functionValue =
        builder_.CreateCall(loadFunction, {name}, "global.func");
    builder_.CreateBr(doneBlock);
    functionBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(builtinBlock);
    auto *builtinValue =
        builder_.CreateCall(loadBuiltin, {name}, "global.builtin");
    auto *hasBuiltin =
        builder_.CreateICmpNE(builtinValue, constantValue(nilBits()),
                              "has.global.builtin");
    builder_.CreateCondBr(hasBuiltin, doneBlock, missingBlock);
    builtinBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(missingBlock);
    std::string message = "Undefined variable '" + instruction.symbol + "'.";
    builder_.CreateCall(runtimeError,
                        {stringPtr(message, "undefined.global.message")});
    builder_.CreateRet(nilValue());

    builder_.SetInsertPoint(doneBlock);
    auto *phi = builder_.CreatePHI(valueTy(), 3, "global");
    phi->addIncoming(varValue, varBlock);
    phi->addIncoming(functionValue, functionBlock);
    phi->addIncoming(builtinValue, builtinBlock);
    bind(instruction, phi, instruction.resultType);
    return std::nullopt;
  }

  std::optional<std::string> emitLoadLocal(const Instruction &instruction) {
    if (instruction.symbol.empty()) {
      return unsupported(instruction, "missing local symbol");
    }
    auto *slot = localSlot(instruction.symbol);
    if (!slot) {
      return unsupported(instruction, "missing elx_allocate_value_slot");
    }
    auto *value = builder_.CreateLoad(valueTy(), slot, instruction.symbol);
    bind(instruction, value, instruction.resultType);
    if (instruction.result) {
      auto it = localClassTargets_.find(instruction.symbol);
      if (it != localClassTargets_.end()) {
        classValueTargets_[instruction.result->id] = it->second;
      }
    }
    return std::nullopt;
  }

  std::optional<std::string> emitStoreLocal(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 1)) {
      return failure;
    }
    if (instruction.symbol.empty()) {
      return unsupported(instruction, "missing local symbol");
    }
    if (instruction.declaresSymbol) {
      llvm::Value *slot = nullptr;
      if (isCapturedLocal(instruction.symbol)) {
        slot = createHeapSlot(lookup(instruction.operands[0]),
                              instruction.symbol);
      } else {
        slot = localSlot(instruction.symbol);
        if (slot) {
          builder_.CreateStore(lookup(instruction.operands[0]), slot);
        }
      }
      if (!slot) {
        return unsupported(instruction, "missing local slot");
      }
      locals_[instruction.symbol] = slot;
      if (auto target = classTarget(instruction.operands[0]);
          target &&
          reassignedLocals_.find(instruction.symbol) ==
              reassignedLocals_.end() &&
          capturedLocals_.find(instruction.symbol) == capturedLocals_.end()) {
        localClassTargets_[instruction.symbol] = *target;
      } else {
        localClassTargets_.erase(instruction.symbol);
      }
      return std::nullopt;
    }
    auto *slot = localSlot(instruction.symbol);
    if (!slot) {
      return unsupported(instruction, "missing elx_allocate_value_slot");
    }
    builder_.CreateStore(lookup(instruction.operands[0]), slot);
    localClassTargets_.erase(instruction.symbol);
    return std::nullopt;
  }

  const Upvalue *findUpvalue(const std::string &name) const {
    if (!loxFunction_) {
      return nullptr;
    }
    for (const Upvalue &upvalue : loxFunction_->upvalues()) {
      if (upvalue.name == name) {
        return &upvalue;
      }
    }
    return nullptr;
  }

  llvm::Value *loadUpvalueObject(uint32_t index) {
    if (!upvalueArray_) {
      return nullptr;
    }
    auto *slot = builder_.CreateGEP(valueTy(), upvalueArray_,
                                    constantI32(static_cast<int>(index)),
                                    "upvalue.slot");
    return builder_.CreateLoad(valueTy(), slot, "upvalue.object");
  }

  std::optional<std::string> emitLoadUpvalue(const Instruction &instruction) {
    const Upvalue *upvalue = findUpvalue(instruction.symbol);
    auto *get = runtime("elx_get_upvalue_value");
    if (!upvalue || !get) {
      return unsupported(instruction, "missing upvalue runtime state");
    }
    auto *upvalueObject = loadUpvalueObject(
        static_cast<uint32_t>(upvalue - loxFunction_->upvalues().data()));
    if (!upvalueObject) {
      return unsupported(instruction, "missing upvalue array");
    }
    auto *value = builder_.CreateCall(get, {upvalueObject}, "upvalue.value");
    guardRuntimeError();
    bind(instruction, value, instruction.resultType);
    return std::nullopt;
  }

  std::optional<std::string> emitStoreUpvalue(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 1)) {
      return failure;
    }
    const Upvalue *upvalue = findUpvalue(instruction.symbol);
    auto *set = runtime("elx_set_upvalue_value");
    if (!upvalue || !set) {
      return unsupported(instruction, "missing upvalue runtime state");
    }
    auto *upvalueObject = loadUpvalueObject(
        static_cast<uint32_t>(upvalue - loxFunction_->upvalues().data()));
    if (!upvalueObject) {
      return unsupported(instruction, "missing upvalue array");
    }
    builder_.CreateCall(set, {upvalueObject, lookup(instruction.operands[0])});
    guardRuntimeError();
    return std::nullopt;
  }

  std::optional<std::string> emitStoreGlobal(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 1)) {
      return failure;
    }
    auto *store = runtime("elx_set_global_variable");
    auto *hasVariable = runtime("elx_has_global_variable");
    auto *hasFunction = runtime("elx_has_global_function");
    auto *runtimeError = runtime("elx_runtime_error");
    if (!store || !hasVariable || !hasFunction || !runtimeError) {
      return unsupported(instruction, "missing global runtime helper");
    }
    auto *name = stringPtr(instruction.symbol, "global.name");
    if (!instruction.declaresSymbol) {
      auto *hasVar = builder_.CreateICmpNE(
          builder_.CreateCall(hasVariable, {name}, "has.assign.var"),
          constantI32(0), "has.assign.var.bool");
      auto *hasFunc = builder_.CreateICmpNE(
          builder_.CreateCall(hasFunction, {name}, "has.assign.func"),
          constantI32(0), "has.assign.func.bool");
      auto *defined = builder_.CreateOr(hasVar, hasFunc, "assign.defined");

      auto *storeBlock =
          llvm::BasicBlock::Create(ctx_, "assign.store", function_);
      auto *missingBlock =
          llvm::BasicBlock::Create(ctx_, "assign.missing", function_);
      builder_.CreateCondBr(defined, storeBlock, missingBlock);

      builder_.SetInsertPoint(missingBlock);
      std::string message = "Undefined variable '" + instruction.symbol + "'.";
      builder_.CreateCall(runtimeError,
                          {stringPtr(message, "undefined.assign.message")});
      builder_.CreateRet(nilValue());

      builder_.SetInsertPoint(storeBlock);
    }
    builder_.CreateCall(store, {name, lookup(instruction.operands[0])});
    return std::nullopt;
  }

  llvm::Value *captureUpvalue(const Upvalue &upvalue) {
    if (upvalue.source == UpvalueSourceKind::Upvalue) {
      return loadUpvalueObject(upvalue.sourceIndex);
    }

    auto *allocateUpvalue = runtime("elx_allocate_upvalue");
    if (!allocateUpvalue) {
      return nullptr;
    }
    auto *slot = localSlot(upvalue.sourceSymbol);
    if (!slot) {
      return nullptr;
    }
    return builder_.CreateCall(allocateUpvalue, {slot}, "captured.upvalue");
  }

  std::optional<std::string> emitDefineFunction(const Instruction &instruction) {
    auto functionIt = functions_.find(instruction.symbol);
    auto loxIt = loxFunctions_.find(instruction.symbol);
    auto *allocateFunction = runtime("elx_allocate_function");
    auto *setFunctionFlags = runtime("elx_set_function_flags");
    if (functionIt == functions_.end() || loxIt == loxFunctions_.end() ||
        !allocateFunction || !setFunctionFlags) {
      return unsupported(instruction, "missing function declaration");
    }

    const LoxFunction &target = *loxIt->second;
    auto *name = stringPtr(target.displayName(), "function.name");
    auto *arity = constantI32(target.arity());
    auto *functionPtr = builder_.CreateBitCast(functionIt->second, i8PtrTy(),
                                               "function.ptr");
    auto *functionObject = builder_.CreateCall(
        allocateFunction, {name, arity, functionPtr}, "function.object");
    guardRuntimeError();
    uint32_t flags = functionRuntimeFlags(target);
    if (flags != 0) {
      builder_.CreateCall(setFunctionFlags,
                          {functionObject, constantI32(static_cast<int>(flags))});
    }

    if (target.upvalues().empty() && !target.isMethod()) {
      bind(instruction, functionObject, LoxType::Function);
      if (instruction.result) {
        functionValueTargets_[instruction.result->id] = instruction.symbol;
      }
      return std::nullopt;
    }

    auto *allocateClosure = runtime("elx_allocate_closure");
    auto *setUpvalue = runtime("elx_set_closure_upvalue");
    if (!allocateClosure || !setUpvalue) {
      return unsupported(instruction, "missing closure runtime helper");
    }

    auto *closure = builder_.CreateCall(
        allocateClosure,
        {functionObject, constantI32(static_cast<int>(target.upvalues().size()))},
        "closure.object");
    guardRuntimeError();

    for (size_t index = 0; index < target.upvalues().size(); ++index) {
      auto *captured = captureUpvalue(target.upvalues()[index]);
      if (!captured) {
        return unsupported(instruction, "failed to capture upvalue");
      }
      builder_.CreateCall(setUpvalue,
                          {closure, constantI32(static_cast<int>(index)),
                           captured});
      guardRuntimeError();
    }

    bind(instruction, closure, LoxType::Closure);
    if (instruction.result) {
      functionValueTargets_[instruction.result->id] = instruction.symbol;
    }
    return std::nullopt;
  }

  std::optional<std::string> emitBuiltinCall(const Instruction &instruction) {
    if (!instruction.operands.empty() || !instruction.arguments.empty()) {
      return unsupported(instruction, "unexpected builtin call operands");
    }

    if (instruction.symbol == "clock") {
      auto *clock = runtime("elx_clock");
      if (!clock) {
        return unsupported(instruction, "missing elx_clock");
      }
      bind(instruction, builder_.CreateCall(clock, {}, "builtin.clock"),
           LoxType::Number);
      return std::nullopt;
    }

    if (instruction.symbol == "readLine") {
      auto *readLine = runtime("elx_readLine");
      if (!readLine) {
        return unsupported(instruction, "missing elx_readLine");
      }
      bind(instruction, builder_.CreateCall(readLine, {}, "builtin.readline"),
           instruction.resultType);
      return std::nullopt;
    }

    return unsupported(instruction, "unknown builtin call");
  }

  std::optional<std::string>
  tryEmitKnownClassCall(const Instruction &instruction, llvm::Value *callee,
                        bool &emitted) {
    emitted = false;
    auto classKey = classTarget(instruction.operands[0]);
    if (!classKey) {
      return std::nullopt;
    }
    auto classIt = knownClasses_.find(*classKey);
    if (classIt == knownClasses_.end()) {
      return std::nullopt;
    }

    const KnownClass &knownClass = classIt->second;
    if (knownClass.initializerFunction.empty()) {
      if (knownClass.hasSuperclass || !instruction.arguments.empty()) {
        return std::nullopt;
      }
      auto *instantiateKnownClass = runtime("elx_instantiate_known_class");
      if (!instantiateKnownClass) {
        return unsupported(instruction, "missing elx_instantiate_known_class");
      }
      auto *instance = builder_.CreateCall(instantiateKnownClass, {callee},
                                           "known.class.instance");
      guardRuntimeError();
      bind(instruction, instance, LoxType::Instance);
      emitted = true;
      return std::nullopt;
    }

    auto functionIt = functions_.find(knownClass.initializerFunction);
    auto loxIt = loxFunctions_.find(knownClass.initializerFunction);
    if (functionIt == functions_.end() || loxIt == loxFunctions_.end()) {
      return std::nullopt;
    }
    const LoxFunction &initializer = *loxIt->second;
    if (!initializer.upvalues().empty()) {
      return std::nullopt;
    }
    const int expectedArity =
        initializer.arity() > 0 ? initializer.arity() - 1 : 0;
    if (static_cast<int>(instruction.arguments.size()) != expectedArity) {
      return std::nullopt;
    }
    auto *instantiateKnownClass = runtime("elx_instantiate_known_class");
    if (!instantiateKnownClass) {
      return unsupported(instruction, "missing elx_instantiate_known_class");
    }

    auto *instance = builder_.CreateCall(instantiateKnownClass, {callee},
                                         "known.class.instance");
    guardRuntimeError();

    std::vector<llvm::Value *> directArgs;
    directArgs.reserve(instruction.arguments.size() + 1);
    directArgs.push_back(instance);
    for (ValueId id : instruction.arguments) {
      directArgs.push_back(lookup(id));
    }

    if (isLeafFunction(initializer)) {
      builder_.CreateCall(functionIt->second, directArgs,
                          "known.class.initializer.leaf");
      if (!functionCannotRaiseRuntimeError(initializer)) {
        guardRuntimeError();
      }
      bind(instruction, instance, LoxType::Instance);
      emitted = true;
      return std::nullopt;
    }

    auto *overflowBlock =
        llvm::BasicBlock::Create(ctx_, "known.class.init.overflow", function_);
    auto *callBlock =
        llvm::BasicBlock::Create(ctx_, "known.class.init.call", function_);
    auto *doneBlock =
        llvm::BasicBlock::Create(ctx_, "known.class.init.done", function_);
    emitEnterCallFrame(callBlock, overflowBlock, "known.class.init");

    builder_.SetInsertPoint(overflowBlock);
    emitStackOverflowReturn();

    builder_.SetInsertPoint(callBlock);
    builder_.CreateCall(functionIt->second, directArgs,
                        "known.class.initializer");
    emitLeaveCallFrame("known.class.init");
    guardRuntimeError();
    builder_.CreateBr(doneBlock);

    builder_.SetInsertPoint(doneBlock);
    bind(instruction, instance, LoxType::Instance);
    emitted = true;
    return std::nullopt;
  }

  std::optional<std::string> emitCall(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 1)) {
      return failure;
    }
    for (ValueId id : instruction.arguments) {
      if (!lookup(id)) {
        return unsupported(instruction, "argument was not defined");
      }
    }
    auto *call = runtime("elx_call_value");
    if (!call) {
      return unsupported(instruction, "missing elx_call_value");
    }
    auto *count = constantI32(static_cast<int>(instruction.arguments.size()));
    if (typeOf(instruction.operands[0]) == LoxType::Class) {
      bool emittedKnownClassCall = false;
      if (auto failure = tryEmitKnownClassCall(
              instruction, lookup(instruction.operands[0]),
              emittedKnownClassCall)) {
        return failure;
      }
      if (emittedKnownClassCall) {
        return std::nullopt;
      }

      auto *args = emitArgumentArray(instruction.arguments, "call.args");
      auto *callClassFast = runtime("elx_call_class_fast");
      auto *instantiateKnownClass = runtime("elx_instantiate_known_class");
      auto *updateCache = runtime("elx_call_cache_update");
      if (callClassFast && instantiateKnownClass && updateCache) {
        auto *callee = lookup(instruction.operands[0]);
        auto *cache = createCallCache(instruction);
        auto *cachedCallee =
            loadCallCacheI64(cache, 0, "class.call.cached.callee");
        auto *initializer =
            loadCallCacheI64(cache, 1, "class.call.cached.initializer");
        auto *targetPtr =
            loadCallCachePtr(cache, 3, "class.call.cached.target");
        auto *expectedArity =
            loadCallCacheI32(cache, 4, "class.call.cached.arity");
        auto *cachedKind =
            loadCallCacheI32(cache, 5, "class.call.cached.kind");
        auto *flags = loadCallCacheI32(cache, 6, "class.call.cached.flags");

        auto *calleeMatches =
            builder_.CreateICmpEQ(cachedCallee, callee,
                                  "class.call.callee.matches");
        auto *kindMatches = builder_.CreateICmpEQ(
            cachedKind,
            llvm::ConstantInt::get(
                i32Ty(), static_cast<int>(CallInlineCacheKind::CLASS)),
            "class.call.kind.matches");
        auto *cacheHit = builder_.CreateAnd(calleeMatches, kindMatches,
                                            "class.call.cache.hit");

        auto *fastBlock =
            llvm::BasicBlock::Create(ctx_, "class.call.fast", function_);
        auto *fallbackBlock =
            llvm::BasicBlock::Create(ctx_, "class.call.generic", function_);
        auto *doneBlock =
            llvm::BasicBlock::Create(ctx_, "class.call.done", function_);
        builder_.CreateCondBr(cacheHit, fastBlock, fallbackBlock);

        builder_.SetInsertPoint(fastBlock);
        auto *hasInitializer = builder_.CreateICmpNE(
            builder_.CreateAnd(flags,
                               constantI32(CALL_CACHE_FLAG_CLASS_HAS_INITIALIZER),
                               "class.call.initializer.flag"),
            constantI32(0), "class.call.has.initializer");
        auto *zeroArgs =
            builder_.CreateICmpEQ(count, constantI32(0), "class.call.zero.args");
        auto *canSkipInitializer =
            builder_.CreateAnd(builder_.CreateNot(hasInitializer), zeroArgs,
                               "class.call.no.initializer.direct");
        auto *directMethodFlags = builder_.CreateAnd(
            flags,
            constantI32(CALL_CACHE_FLAG_METHOD_IS_CLOSURE |
                        CALL_CACHE_FLAG_METHOD_IS_FUNCTION),
            "class.call.direct.flags");
        auto *isDirectCallable =
            builder_.CreateICmpNE(directMethodFlags, constantI32(0),
                                  "class.call.direct.callable");
        auto *hasUpvalues = builder_.CreateICmpNE(
            builder_.CreateAnd(flags,
                               constantI32(CALL_CACHE_FLAG_CLOSURE_HAS_UPVALUES),
                               "class.call.upvalue.flag"),
            constantI32(0), "class.call.has.upvalues");
        auto *targetLeaf = builder_.CreateICmpNE(
            builder_.CreateAnd(flags, constantI32(CALL_CACHE_FLAG_TARGET_LEAF),
                               "class.call.leaf.flag"),
            constantI32(0), "class.call.target.leaf");
        auto *targetCannotError = builder_.CreateICmpNE(
            builder_.CreateAnd(
                flags, constantI32(CALL_CACHE_FLAG_TARGET_NO_RUNTIME_ERROR),
                "class.call.noerror.flag"),
            constantI32(0), "class.call.target.noerror");
        auto *arityMatches =
            builder_.CreateICmpEQ(expectedArity, count,
                                  "class.call.arity.matches");
        auto *targetAvailable =
            builder_.CreateICmpNE(targetPtr,
                                  llvm::ConstantPointerNull::get(i8PtrTy()),
                                  "class.call.target.available");
        auto *canDirectInitialize =
            builder_.CreateAnd(hasInitializer, isDirectCallable,
                               "class.call.direct.init.0");
        canDirectInitialize = builder_.CreateAnd(
            canDirectInitialize, builder_.CreateNot(hasUpvalues),
            "class.call.direct.init.1");
        canDirectInitialize = builder_.CreateAnd(
            canDirectInitialize, arityMatches, "class.call.direct.init.2");
        canDirectInitialize =
            builder_.CreateAnd(canDirectInitialize, targetAvailable,
                               "class.call.direct.init");
        auto *canDirectClass = builder_.CreateOr(
            canSkipInitializer, canDirectInitialize, "class.call.direct");

        auto *directBlock =
            llvm::BasicBlock::Create(ctx_, "class.call.direct", function_);
        auto *directInitBlock =
            llvm::BasicBlock::Create(ctx_, "class.call.direct.init",
                                     function_);
        auto *directInitLeafBlock =
            llvm::BasicBlock::Create(ctx_, "class.call.direct.init.leaf",
                                     function_);
        auto *directInitLeafCheckBlock = llvm::BasicBlock::Create(
            ctx_, "class.call.direct.init.leaf.check", function_);
        auto *directInitFramedBlock =
            llvm::BasicBlock::Create(ctx_, "class.call.direct.init.framed",
                                     function_);
        auto *directNoInitBlock =
            llvm::BasicBlock::Create(ctx_, "class.call.direct.noinit",
                                     function_);
        auto *directOverflowBlock =
            llvm::BasicBlock::Create(ctx_, "class.call.direct.overflow",
                                     function_);
        auto *directInitCallBlock =
            llvm::BasicBlock::Create(ctx_, "class.call.direct.init.call",
                                     function_);
        auto *helperBlock =
            llvm::BasicBlock::Create(ctx_, "class.call.fast.helper",
                                     function_);
        builder_.CreateCondBr(canDirectClass, directBlock, helperBlock);

        builder_.SetInsertPoint(directBlock);
        auto *instance = builder_.CreateCall(instantiateKnownClass, {callee},
                                             "class.call.instance");
        guardRuntimeError();
        builder_.CreateCondBr(canDirectInitialize, directInitBlock,
                              directNoInitBlock);

        builder_.SetInsertPoint(directInitBlock);
        builder_.CreateCondBr(targetLeaf, directInitLeafBlock,
                              directInitFramedBlock);

        std::vector<llvm::Type *> directParamTypes(instruction.arguments.size() + 1,
                                                   valueTy());
        auto *directFunctionTy =
            llvm::FunctionType::get(valueTy(), directParamTypes, false);
        std::vector<llvm::Value *> directArgs;
        directArgs.reserve(instruction.arguments.size() + 1);
        directArgs.push_back(instance);
        for (ValueId id : instruction.arguments) {
          directArgs.push_back(lookup(id));
        }

        builder_.SetInsertPoint(directInitLeafBlock);
        builder_.CreateCall(directFunctionTy, targetPtr, directArgs,
                            "class.call.initializer.direct.leaf");
        builder_.CreateCondBr(targetCannotError, doneBlock,
                              directInitLeafCheckBlock);
        directInitLeafBlock = builder_.GetInsertBlock();

        builder_.SetInsertPoint(directInitLeafCheckBlock);
        guardRuntimeError();
        builder_.CreateBr(doneBlock);
        directInitLeafCheckBlock = builder_.GetInsertBlock();

        builder_.SetInsertPoint(directInitFramedBlock);
        emitEnterCallFrame(directInitCallBlock, directOverflowBlock,
                           "class.call.init");

        builder_.SetInsertPoint(directOverflowBlock);
        emitStackOverflowReturn();

        builder_.SetInsertPoint(directInitCallBlock);
        builder_.CreateCall(directFunctionTy, targetPtr, directArgs,
                            "class.call.initializer.direct");
        emitLeaveCallFrame("class.call.init");
        guardRuntimeError();
        builder_.CreateBr(doneBlock);
        directInitCallBlock = builder_.GetInsertBlock();

        builder_.SetInsertPoint(directNoInitBlock);
        builder_.CreateBr(doneBlock);
        directNoInitBlock = builder_.GetInsertBlock();

        builder_.SetInsertPoint(helperBlock);
        auto *fastResult = builder_.CreateCall(
            callClassFast, {callee, args, count, initializer, targetPtr,
                            expectedArity, flags},
            "class.call.fast.result");
        guardRuntimeError();
        builder_.CreateBr(doneBlock);
        helperBlock = builder_.GetInsertBlock();

        builder_.SetInsertPoint(fallbackBlock);
        auto *genericResult =
            builder_.CreateCall(call, {callee, args, count}, "class.call");
        guardRuntimeError();
        builder_.CreateCall(updateCache,
                            {builder_.CreatePointerCast(cache, callCachePtrTy()),
                             callee});
        builder_.CreateBr(doneBlock);
        fallbackBlock = builder_.GetInsertBlock();

        builder_.SetInsertPoint(doneBlock);
        auto *result = builder_.CreatePHI(valueTy(), 6, "class.call.result");
        result->addIncoming(instance, directInitLeafBlock);
        result->addIncoming(instance, directInitLeafCheckBlock);
        result->addIncoming(instance, directInitCallBlock);
        result->addIncoming(instance, directNoInitBlock);
        result->addIncoming(fastResult, helperBlock);
        result->addIncoming(genericResult, fallbackBlock);
        bind(instruction, result, instruction.resultType);
        return std::nullopt;
      }
    }
    auto *args = emitArgumentArray(instruction.arguments, "call.args");
    auto *result =
        builder_.CreateCall(call, {lookup(instruction.operands[0]), args, count},
                            "call");
    guardRuntimeError();
    bind(instruction, result, instruction.resultType);
    return std::nullopt;
  }

  std::optional<std::string> emitDirectCall(const Instruction &instruction) {
    for (ValueId id : instruction.arguments) {
      if (!lookup(id)) {
        return unsupported(instruction, "argument was not defined");
      }
    }
    auto functionIt = functions_.find(instruction.symbol);
    auto loxIt = loxFunctions_.find(instruction.symbol);
    if (functionIt == functions_.end() || loxIt == loxFunctions_.end()) {
      return unsupported(instruction, "missing direct call target");
    }
    if (!loxIt->second->upvalues().empty()) {
      return unsupported(instruction, "direct closure calls are not supported");
    }

    if (isLeafFunction(*loxIt->second)) {
      std::vector<llvm::Value *> args;
      args.reserve(instruction.arguments.size());
      for (ValueId id : instruction.arguments) {
        args.push_back(lookup(id));
      }
      auto *result =
          builder_.CreateCall(functionIt->second, args, "direct.call.leaf");
      if (!functionCannotRaiseRuntimeError(*loxIt->second)) {
        guardRuntimeError();
      }
      bind(instruction, result, instruction.resultType);
      return std::nullopt;
    }

    auto *overflowBlock =
        llvm::BasicBlock::Create(ctx_, "direct.call.overflow", function_);
    auto *callBlock =
        llvm::BasicBlock::Create(ctx_, "direct.call.body", function_);
    emitEnterCallFrame(callBlock, overflowBlock, "direct.call");

    builder_.SetInsertPoint(overflowBlock);
    emitStackOverflowReturn();

    builder_.SetInsertPoint(callBlock);
    std::vector<llvm::Value *> args;
    args.reserve(instruction.arguments.size());
    for (ValueId id : instruction.arguments) {
      args.push_back(lookup(id));
    }
    auto *result =
        builder_.CreateCall(functionIt->second, args, "direct.call");
    emitLeaveCallFrame("direct.call");
    guardRuntimeError();
    bind(instruction, result, instruction.resultType);
    return std::nullopt;
  }

  std::optional<std::string>
  emitPreparePropertyCall(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 1)) {
      return failure;
    }
    if (!instruction.auxResult) {
      return unsupported(instruction, "missing prepared-call kind result");
    }
    auto *prepare = runtime("elx_prepare_property_call_cached");
    if (!prepare) {
      return unsupported(instruction,
                         "missing elx_prepare_property_call_cached");
    }
    auto *name = emitInternedName(instruction.symbol, "property.call.name");
    auto *targetSlot = createStackSlot("property.call.target");
    builder_.CreateStore(nilValue(), targetSlot);
    auto *cache = createCallCache(instruction);
    if (instruction.result) {
      preparedCallCaches_[instruction.result->id] = cache;
    }
    preparedCallCaches_[instruction.auxResult->id] = cache;
    auto *receiver = lookup(instruction.operands[0]);

    auto *objectBlock = llvm::BasicBlock::Create(
        ctx_, "property.call.fast.object", function_);
    auto *instanceBlock = llvm::BasicBlock::Create(
        ctx_, "property.call.fast.instance", function_);
    auto *hitBlock =
        llvm::BasicBlock::Create(ctx_, "property.call.fast.hit", function_);
    auto *fallbackBlock =
        llvm::BasicBlock::Create(ctx_, "property.call.prepare", function_);
    auto *doneBlock =
        llvm::BasicBlock::Create(ctx_, "property.call.ready", function_);

    llvm::Value *objectPtr = nullptr;
    if (typeOf(instruction.operands[0]) == LoxType::Instance) {
      objectPtr = objectPointer(receiver, "property.call.object");
      builder_.CreateBr(instanceBlock);
      builder_.SetInsertPoint(objectBlock);
      builder_.CreateUnreachable();
    } else {
      builder_.CreateCondBr(isObjectValue(receiver), objectBlock,
                            fallbackBlock);

      builder_.SetInsertPoint(objectBlock);
      objectPtr = objectPointer(receiver, "property.call.object");
      auto *objectType = loadI32AtOffset(objectPtr, offsetof(Obj, type),
                                         "property.call.object.type");
      auto *isInstance = builder_.CreateICmpEQ(
          objectType,
          llvm::ConstantInt::get(i32Ty(), static_cast<int>(ObjType::INSTANCE)),
          "property.call.is.instance");
      builder_.CreateCondBr(isInstance, instanceBlock, fallbackBlock);
    }

    builder_.SetInsertPoint(instanceBlock);
    auto *shape = loadPointerAtOffset(objectPtr, offsetof(ObjInstance, shape),
                                      "property.call.shape");
    auto *klass = loadPointerAtOffset(objectPtr, offsetof(ObjInstance, klass),
                                      "property.call.class");
    auto *shapeBits =
        builder_.CreatePtrToInt(shape, i64Ty(), "property.call.shape.bits");
    auto *klassBits =
        builder_.CreatePtrToInt(klass, i64Ty(), "property.call.class.bits");
    auto *cachedShape =
        loadCallCacheI64(cache, 0, "property.call.cached.shape");
    auto *cachedMethod =
        loadCallCacheI64(cache, 1, "property.call.cached.method");
    auto *cachedClass =
        loadCallCacheI64(cache, 2, "property.call.cached.class");
    auto *cachedKind =
        loadCallCacheI32(cache, 5, "property.call.cached.kind");
    auto *kindMatches = builder_.CreateICmpEQ(
        cachedKind,
        llvm::ConstantInt::get(
            i32Ty(), static_cast<int>(CallInlineCacheKind::BOUND_METHOD)),
        "property.call.kind.matches");
    auto *shapeMatches =
        builder_.CreateICmpEQ(cachedShape, shapeBits,
                              "property.call.shape.matches");
    auto *classMatches =
        builder_.CreateICmpEQ(cachedClass, klassBits,
                              "property.call.class.matches");
    auto *hasMethod =
        builder_.CreateICmpNE(cachedMethod, constantI64(0),
                              "property.call.has.method");
    auto *cacheHit =
        builder_.CreateAnd(kindMatches, shapeMatches, "property.call.hit.0");
    cacheHit = builder_.CreateAnd(cacheHit, classMatches,
                                  "property.call.hit.1");
    cacheHit = builder_.CreateAnd(cacheHit, hasMethod, "property.call.hit");
    builder_.CreateCondBr(cacheHit, hitBlock, fallbackBlock);

    builder_.SetInsertPoint(hitBlock);
    builder_.CreateBr(doneBlock);
    hitBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(fallbackBlock);
    auto *slowKind = builder_.CreateCall(
        prepare,
        {receiver, name, builder_.CreatePointerCast(cache, callCachePtrTy()),
         targetSlot},
        "property.call.kind.slow");
    guardRuntimeError();
    auto *slowTarget =
        builder_.CreateLoad(valueTy(), targetSlot, "property.call.target.slow");
    builder_.CreateBr(doneBlock);
    fallbackBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(doneBlock);
    auto *kind = builder_.CreatePHI(i32Ty(), 2, "property.call.kind");
    kind->addIncoming(llvm::ConstantInt::get(i32Ty(), kPropertyCallMethod),
                      hitBlock);
    kind->addIncoming(slowKind, fallbackBlock);
    auto *target = builder_.CreatePHI(valueTy(), 2, "property.call.target");
    target->addIncoming(cachedMethod, hitBlock);
    target->addIncoming(slowTarget, fallbackBlock);
    bind(instruction, target, instruction.resultType);
    bindValue(*instruction.auxResult, kind);
    return std::nullopt;
  }

  std::optional<std::string>
  emitCallPreparedProperty(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 3)) {
      return failure;
    }
    for (ValueId id : instruction.arguments) {
      if (!lookup(id)) {
        return unsupported(instruction, "argument was not defined");
      }
    }
    auto *call = runtime("elx_call_prepared_property");
    if (!call) {
      return unsupported(instruction, "missing elx_call_prepared_property");
    }
    auto *callMethodFast = runtime("elx_call_method_fast");
    auto *args =
        emitArgumentArray(instruction.arguments, "prepared.property.args");
    auto *count = constantI32(static_cast<int>(instruction.arguments.size()));

    auto cacheIt = preparedCallCaches_.find(instruction.operands[2].id);
    if (cacheIt == preparedCallCaches_.end() || !callMethodFast) {
      auto *result = builder_.CreateCall(
          call,
          {lookup(instruction.operands[1]), lookup(instruction.operands[0]),
           lookup(instruction.operands[2]), args, count},
          "prepared.property.call");
      guardRuntimeError();
      bind(instruction, result, instruction.resultType);
      return std::nullopt;
    }

    auto *receiver = lookup(instruction.operands[0]);
    auto *kind = lookup(instruction.operands[1]);
    auto *target = lookup(instruction.operands[2]);
    auto *cache = cacheIt->second;
    auto *cachedMethod =
        loadCallCacheI64(cache, 1, "prepared.property.cached.method");
    auto *targetPtr =
        loadCallCachePtr(cache, 3, "prepared.property.cached.target");
    auto *expectedArity =
        loadCallCacheI32(cache, 4, "prepared.property.cached.arity");
    auto *cachedKind =
        loadCallCacheI32(cache, 5, "prepared.property.cached.kind");
    auto *flags = loadCallCacheI32(cache, 6, "prepared.property.cached.flags");

    auto *isMethod = builder_.CreateICmpEQ(
        kind, llvm::ConstantInt::get(i32Ty(), kPropertyCallMethod),
        "prepared.property.is.method");
    auto *cacheIsMethod = builder_.CreateICmpEQ(
        cachedKind,
        llvm::ConstantInt::get(
            i32Ty(), static_cast<int>(CallInlineCacheKind::BOUND_METHOD)),
        "prepared.property.cache.is.method");
    auto *targetMatches = builder_.CreateICmpEQ(
        cachedMethod, target, "prepared.property.target.matches");
    auto *targetAvailable = builder_.CreateICmpNE(
        targetPtr, llvm::ConstantPointerNull::get(i8PtrTy()),
        "prepared.property.target.available");
    auto *fastEligible =
        builder_.CreateAnd(isMethod, cacheIsMethod, "prepared.property.fast.0");
    fastEligible = builder_.CreateAnd(fastEligible, targetMatches,
                                      "prepared.property.fast.1");
    fastEligible = builder_.CreateAnd(fastEligible, targetAvailable,
                                      "prepared.property.fast");
    auto *directMethodFlags = builder_.CreateAnd(
        flags,
        constantI32(CALL_CACHE_FLAG_METHOD_IS_CLOSURE |
                    CALL_CACHE_FLAG_METHOD_IS_FUNCTION),
        "prepared.property.direct.flags");
    auto *isDirectCallable =
        builder_.CreateICmpNE(directMethodFlags, constantI32(0),
                              "prepared.property.direct.callable");
    auto *hasUpvalues = builder_.CreateICmpNE(
        builder_.CreateAnd(flags,
                           constantI32(CALL_CACHE_FLAG_CLOSURE_HAS_UPVALUES),
                           "prepared.property.upvalue.flag"),
        constantI32(0), "prepared.property.has.upvalues");
    auto *targetLeaf = builder_.CreateICmpNE(
        builder_.CreateAnd(flags, constantI32(CALL_CACHE_FLAG_TARGET_LEAF),
                           "prepared.property.leaf.flag"),
        constantI32(0), "prepared.property.target.leaf");
    auto *targetCannotError = builder_.CreateICmpNE(
        builder_.CreateAnd(
            flags, constantI32(CALL_CACHE_FLAG_TARGET_NO_RUNTIME_ERROR),
            "prepared.property.noerror.flag"),
        constantI32(0), "prepared.property.target.noerror");
    auto *arityMatches =
        builder_.CreateICmpEQ(expectedArity, count,
                              "prepared.property.arity.matches");
    auto *canDirectCall =
        builder_.CreateAnd(fastEligible, isDirectCallable,
                           "prepared.property.direct.0");
    canDirectCall = builder_.CreateAnd(
        canDirectCall, builder_.CreateNot(hasUpvalues),
        "prepared.property.direct.1");
    canDirectCall =
        builder_.CreateAnd(canDirectCall, arityMatches,
                           "prepared.property.direct");
    auto *fastBlock =
        llvm::BasicBlock::Create(ctx_, "prepared.property.fast", function_);
    auto *directBlock =
        llvm::BasicBlock::Create(ctx_, "prepared.property.direct", function_);
    auto *directLeafBlock = llvm::BasicBlock::Create(
        ctx_, "prepared.property.direct.leaf", function_);
    auto *directLeafCheckBlock = llvm::BasicBlock::Create(
        ctx_, "prepared.property.direct.leaf.check", function_);
    auto *directFramedBlock = llvm::BasicBlock::Create(
        ctx_, "prepared.property.direct.framed", function_);
    auto *directOverflowBlock = llvm::BasicBlock::Create(
        ctx_, "prepared.property.direct.overflow", function_);
    auto *directCallBlock = llvm::BasicBlock::Create(
        ctx_, "prepared.property.direct.call", function_);
    auto *helperBlock = llvm::BasicBlock::Create(
        ctx_, "prepared.property.fast.helper", function_);
    auto *fallbackBlock =
        llvm::BasicBlock::Create(ctx_, "prepared.property.generic", function_);
    auto *doneBlock =
        llvm::BasicBlock::Create(ctx_, "prepared.property.done", function_);
    builder_.CreateCondBr(fastEligible, fastBlock, fallbackBlock);

    builder_.SetInsertPoint(fastBlock);
    builder_.CreateCondBr(canDirectCall, directBlock, helperBlock);
    fastBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(directBlock);
    builder_.CreateCondBr(targetLeaf, directLeafBlock, directFramedBlock);

    std::vector<llvm::Type *> directParamTypes(instruction.arguments.size() + 1,
                                               valueTy());
    auto *directFunctionTy =
        llvm::FunctionType::get(valueTy(), directParamTypes, false);
    std::vector<llvm::Value *> directArgs;
    directArgs.reserve(instruction.arguments.size() + 1);
    directArgs.push_back(receiver);
    for (ValueId id : instruction.arguments) {
      directArgs.push_back(lookup(id));
    }

    builder_.SetInsertPoint(directLeafBlock);
    auto *directLeafResult =
        builder_.CreateCall(directFunctionTy, targetPtr, directArgs,
                            "prepared.property.direct.leaf.result");
    builder_.CreateCondBr(targetCannotError, doneBlock, directLeafCheckBlock);
    directLeafBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(directLeafCheckBlock);
    guardRuntimeError();
    builder_.CreateBr(doneBlock);
    directLeafCheckBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(directFramedBlock);
    emitEnterCallFrame(directCallBlock, directOverflowBlock,
                       "prepared.property.direct");

    builder_.SetInsertPoint(directOverflowBlock);
    emitStackOverflowReturn();

    builder_.SetInsertPoint(directCallBlock);
    auto *directResult =
        builder_.CreateCall(directFunctionTy, targetPtr, directArgs,
                            "prepared.property.direct.result");
    emitLeaveCallFrame("prepared.property.direct");
    guardRuntimeError();
    builder_.CreateBr(doneBlock);
    directCallBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(helperBlock);
    auto *fastResult = builder_.CreateCall(
        callMethodFast, {receiver, args, count, target, targetPtr,
                         expectedArity, flags},
        "prepared.property.method.fast");
    guardRuntimeError();
    builder_.CreateBr(doneBlock);
    helperBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(fallbackBlock);
    auto *genericResult = builder_.CreateCall(
        call, {kind, receiver, target, args, count}, "prepared.property.call");
    guardRuntimeError();
    builder_.CreateBr(doneBlock);
    fallbackBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(doneBlock);
    auto *result = builder_.CreatePHI(valueTy(), 5, "prepared.property.result");
    result->addIncoming(directLeafResult, directLeafBlock);
    result->addIncoming(directLeafResult, directLeafCheckBlock);
    result->addIncoming(directResult, directCallBlock);
    result->addIncoming(fastResult, helperBlock);
    result->addIncoming(genericResult, fallbackBlock);
    bind(instruction, result, instruction.resultType);
    return std::nullopt;
  }

  std::optional<std::string> emitGetProperty(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 1)) {
      return failure;
    }
    auto *get = runtime("elx_get_property_slow");
    if (!get) {
      return unsupported(instruction, "missing elx_get_property_slow");
    }
    auto *name = emitInternedName(instruction.symbol, "property.name");
    auto *cache = createPropertyCache(instruction);

    auto *receiver = lookup(instruction.operands[0]);
    auto *objectBlock =
        llvm::BasicBlock::Create(ctx_, "property.fast.object", function_);
    auto *instanceBlock =
        llvm::BasicBlock::Create(ctx_, "property.fast.instance", function_);
    auto *presentBlock =
        llvm::BasicBlock::Create(ctx_, "property.fast.present", function_);
    auto *hitBlock =
        llvm::BasicBlock::Create(ctx_, "property.fast.hit", function_);
    auto *fallbackBlock =
        llvm::BasicBlock::Create(ctx_, "property.slow", function_);
    auto *doneBlock =
        llvm::BasicBlock::Create(ctx_, "property.done", function_);

    llvm::Value *objectPtr = nullptr;
    if (typeOf(instruction.operands[0]) == LoxType::Instance) {
      objectPtr = objectPointer(receiver, "property.object");
      builder_.CreateBr(instanceBlock);
      builder_.SetInsertPoint(objectBlock);
      builder_.CreateUnreachable();
    } else {
      builder_.CreateCondBr(isObjectValue(receiver), objectBlock,
                            fallbackBlock);

      builder_.SetInsertPoint(objectBlock);
      objectPtr = objectPointer(receiver, "property.object");
      auto *objectType = loadI32AtOffset(objectPtr, offsetof(Obj, type),
                                         "property.object.type");
      auto *isInstance = builder_.CreateICmpEQ(
          objectType,
          llvm::ConstantInt::get(i32Ty(), static_cast<int>(ObjType::INSTANCE)),
          "property.is.instance");
      builder_.CreateCondBr(isInstance, instanceBlock, fallbackBlock);
    }

    builder_.SetInsertPoint(instanceBlock);
    auto *shape = loadPointerAtOffset(objectPtr, offsetof(ObjInstance, shape),
                                      "property.shape");
    auto *values = loadPointerAtOffset(
        objectPtr, offsetof(ObjInstance, fieldValues), "property.values");
    auto *presence = loadPointerAtOffset(
        objectPtr, offsetof(ObjInstance, fieldInitialized),
        "property.presence");
    auto *capacity = loadI64AtOffset(
        objectPtr, offsetof(ObjInstance, fieldCapacity), "property.capacity");
    auto *slot = cacheEntrySlot(cache, 0);
    auto *hasEntry = cacheHasEntry(cache, 0);
    auto *shapeMatches =
        builder_.CreateICmpEQ(cacheEntryShape(cache, 0), shape,
                              "property.shape.matches");
    auto *slotInBounds =
        builder_.CreateICmpULT(slot, capacity, "property.slot.in.bounds");
    auto *hasStorage = builder_.CreateAnd(
        builder_.CreateICmpNE(values, nullI8Ptr(), "property.values.nonnull"),
        builder_.CreateICmpNE(presence, nullI8Ptr(),
                              "property.presence.nonnull"),
        "property.has.storage");
    auto *cacheHit =
        builder_.CreateAnd(hasEntry, shapeMatches, "property.cache.shape.hit");
    cacheHit = builder_.CreateAnd(cacheHit, slotInBounds,
                                  "property.cache.bounds.hit");
    cacheHit = builder_.CreateAnd(cacheHit, hasStorage,
                                  "property.cache.storage.hit");
    builder_.CreateCondBr(cacheHit, presentBlock, fallbackBlock);

    builder_.SetInsertPoint(presentBlock);
    auto *presentSlot =
        builder_.CreateGEP(i8Ty(), presence, slot, "property.present.slot");
    auto *present = builder_.CreateICmpNE(
        builder_.CreateLoad(i8Ty(), presentSlot, "property.present"),
        llvm::ConstantInt::get(i8Ty(), 0), "property.is.present");
    builder_.CreateCondBr(present, hitBlock, fallbackBlock);

    builder_.SetInsertPoint(hitBlock);
    auto *fieldSlot =
        builder_.CreateGEP(valueTy(), values, slot, "property.value.slot");
    auto *fastValue = builder_.CreateLoad(valueTy(), fieldSlot, "property.fast");
    builder_.CreateBr(doneBlock);
    hitBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(fallbackBlock);
    auto *slowValue = builder_.CreateCall(
        get,
        {receiver, name, builder_.CreatePointerCast(cache, propertyCachePtrTy()),
         constantI32(static_cast<int>(eloxir::PROPERTY_CACHE_MAX_SIZE))},
        "property.slow");
    guardRuntimeError();
    builder_.CreateBr(doneBlock);
    fallbackBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(doneBlock);
    auto *result = builder_.CreatePHI(valueTy(), 2, "property");
    result->addIncoming(fastValue, hitBlock);
    result->addIncoming(slowValue, fallbackBlock);
    bind(instruction, result, instruction.resultType);
    return std::nullopt;
  }

  std::optional<std::string> emitSetProperty(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 2)) {
      return failure;
    }
    auto *set = runtime("elx_set_property_slow");
    if (!set) {
      return unsupported(instruction, "missing elx_set_property_slow");
    }
    auto *name = emitInternedName(instruction.symbol, "property.name");
    auto *cache = createPropertyCache(instruction);

    auto *receiver = lookup(instruction.operands[0]);
    auto *value = lookup(instruction.operands[1]);
    auto *objectBlock =
        llvm::BasicBlock::Create(ctx_, "set.property.fast.object", function_);
    auto *instanceBlock =
        llvm::BasicBlock::Create(ctx_, "set.property.fast.instance", function_);
    auto *hitBlock =
        llvm::BasicBlock::Create(ctx_, "set.property.fast.hit", function_);
    auto *fallbackBlock =
        llvm::BasicBlock::Create(ctx_, "set.property.slow", function_);
    auto *doneBlock =
        llvm::BasicBlock::Create(ctx_, "set.property.done", function_);

    llvm::Value *objectPtr = nullptr;
    if (typeOf(instruction.operands[0]) == LoxType::Instance) {
      objectPtr = objectPointer(receiver, "set.property.object");
      builder_.CreateBr(instanceBlock);
      builder_.SetInsertPoint(objectBlock);
      builder_.CreateUnreachable();
    } else {
      builder_.CreateCondBr(isObjectValue(receiver), objectBlock,
                            fallbackBlock);

      builder_.SetInsertPoint(objectBlock);
      objectPtr = objectPointer(receiver, "set.property.object");
      auto *objectType = loadI32AtOffset(objectPtr, offsetof(Obj, type),
                                         "set.property.object.type");
      auto *isInstance = builder_.CreateICmpEQ(
          objectType,
          llvm::ConstantInt::get(i32Ty(), static_cast<int>(ObjType::INSTANCE)),
          "set.property.is.instance");
      builder_.CreateCondBr(isInstance, instanceBlock, fallbackBlock);
    }

    builder_.SetInsertPoint(instanceBlock);
    auto *shape = loadPointerAtOffset(objectPtr, offsetof(ObjInstance, shape),
                                      "set.property.shape");
    auto *values = loadPointerAtOffset(
        objectPtr, offsetof(ObjInstance, fieldValues), "set.property.values");
    auto *presence = loadPointerAtOffset(
        objectPtr, offsetof(ObjInstance, fieldInitialized),
        "set.property.presence");
    auto *capacity = loadI64AtOffset(objectPtr,
                                     offsetof(ObjInstance, fieldCapacity),
                                     "set.property.capacity");
    auto *slot = cacheEntrySlot(cache, 0);
    auto *hasEntry = cacheHasEntry(cache, 0);
    auto *shapeMatches =
        builder_.CreateICmpEQ(cacheEntryShape(cache, 0), shape,
                              "set.property.shape.matches");
    auto *slotInBounds =
        builder_.CreateICmpULT(slot, capacity, "set.property.slot.in.bounds");
    auto *hasStorage = builder_.CreateAnd(
        builder_.CreateICmpNE(values, nullI8Ptr(),
                              "set.property.values.nonnull"),
        builder_.CreateICmpNE(presence, nullI8Ptr(),
                              "set.property.presence.nonnull"),
        "set.property.has.storage");
    auto *cacheHit =
        builder_.CreateAnd(hasEntry, shapeMatches, "set.property.cache.shape.hit");
    cacheHit =
        builder_.CreateAnd(cacheHit, slotInBounds,
                           "set.property.cache.bounds.hit");
    cacheHit =
        builder_.CreateAnd(cacheHit, hasStorage,
                           "set.property.cache.storage.hit");
    builder_.CreateCondBr(cacheHit, hitBlock, fallbackBlock);

    builder_.SetInsertPoint(hitBlock);
    auto *fieldSlot =
        builder_.CreateGEP(valueTy(), values, slot, "set.property.value.slot");
    builder_.CreateStore(value, fieldSlot);
    auto *presentSlot =
        builder_.CreateGEP(i8Ty(), presence, slot, "set.property.present.slot");
    builder_.CreateStore(llvm::ConstantInt::get(i8Ty(), 1), presentSlot);
    builder_.CreateBr(doneBlock);
    hitBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(fallbackBlock);
    auto *slowResult = builder_.CreateCall(
        set,
        {receiver, name, value,
         builder_.CreatePointerCast(cache, propertyCachePtrTy()),
         constantI32(static_cast<int>(eloxir::PROPERTY_CACHE_MAX_SIZE))},
        "set.property");
    guardRuntimeError();
    builder_.CreateBr(doneBlock);
    fallbackBlock = builder_.GetInsertBlock();

    builder_.SetInsertPoint(doneBlock);
    auto *result = builder_.CreatePHI(valueTy(), 2, "set.property.result");
    result->addIncoming(value, hitBlock);
    result->addIncoming(slowResult, fallbackBlock);
    bind(instruction, result, instruction.resultType);
    return std::nullopt;
  }

  std::optional<std::string> emitDefineClass(const Instruction &instruction) {
    if (instruction.operands.size() > 1) {
      return unsupported(instruction, "unexpected superclass operand count");
    }
    if (!instruction.operands.empty() && !lookup(instruction.operands[0])) {
      return unsupported(instruction, "superclass operand was not defined");
    }
    auto *allocate = runtime("elx_allocate_class");
    auto *validate = runtime("elx_validate_superclass");
    if (!allocate || !validate) {
      return unsupported(instruction, "missing class runtime helper");
    }
    auto *name = emitInternedName(instruction.symbol, "class.name");
    auto *superclass = nilValue();
    if (!instruction.operands.empty()) {
      superclass = builder_.CreateCall(validate, {lookup(instruction.operands[0])},
                                       "validated.superclass");
      guardRuntimeError();
    }
    auto *klass =
        builder_.CreateCall(allocate, {name, superclass}, "class.object");
    guardRuntimeError();
    bind(instruction, klass, LoxType::Class);
    if (instruction.result) {
      std::string key = (loxFunction_ ? loxFunction_->name() : "<module>") +
                        "#" + std::to_string(instruction.result->id);
      classValueTargets_[instruction.result->id] = key;
      knownClasses_[key] =
          KnownClass{instruction.symbol, !instruction.operands.empty(), ""};
    }
    return std::nullopt;
  }

  std::optional<std::string> emitDefineMethod(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 2)) {
      return failure;
    }
    auto *addMethod = runtime("elx_class_add_method");
    if (!addMethod) {
      return unsupported(instruction, "missing elx_class_add_method");
    }
    auto *name = emitInternedName(instruction.symbol, "method.name");
    builder_.CreateCall(addMethod,
                        {lookup(instruction.operands[0]), name,
                         lookup(instruction.operands[1])});
    guardRuntimeError();
    if (instruction.symbol == "init") {
      auto classKey = classTarget(instruction.operands[0]);
      auto functionName = functionTarget(instruction.operands[1]);
      if (classKey && functionName) {
        auto it = knownClasses_.find(*classKey);
        if (it != knownClasses_.end()) {
          it->second.initializerFunction = *functionName;
        }
      }
    }
    return std::nullopt;
  }

  std::optional<std::string> emitBindSuper(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 2)) {
      return failure;
    }
    auto *findMethod = runtime("elx_class_find_method");
    auto *bindMethod = runtime("elx_bind_method");
    if (!findMethod || !bindMethod) {
      return unsupported(instruction, "missing super runtime helper");
    }
    auto *name = emitInternedName(instruction.symbol, "super.method.name");
    auto *method =
        builder_.CreateCall(findMethod, {lookup(instruction.operands[0]), name},
                            "super.method");
    guardRuntimeError();
    auto *bound = builder_.CreateCall(
        bindMethod, {lookup(instruction.operands[1]), method}, "super.bound");
    guardRuntimeError();
    bind(instruction, bound, instruction.resultType);
    return std::nullopt;
  }

  std::optional<std::string> emitBinary(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 2)) {
      return failure;
    }
    ValueId leftId = instruction.operands[0];
    ValueId rightId = instruction.operands[1];
    auto *left = lookup(leftId);
    auto *right = lookup(rightId);
    LoxType leftType = typeOf(leftId);
    LoxType rightType = typeOf(rightId);

    auto couldBeNumber = [](LoxType type) {
      return type == LoxType::Unknown || type == LoxType::Number;
    };
    auto resultTypeForDynamicNumberOp = [&]() {
      switch (instruction.binaryOp) {
      case BinaryOp::Add:
        return (leftType == LoxType::Number || rightType == LoxType::Number)
                   ? LoxType::Number
                   : LoxType::Unknown;
      case BinaryOp::Subtract:
      case BinaryOp::Multiply:
      case BinaryOp::Divide:
        return LoxType::Number;
      case BinaryOp::Equal:
      case BinaryOp::NotEqual:
      case BinaryOp::Greater:
      case BinaryOp::GreaterEqual:
      case BinaryOp::Less:
      case BinaryOp::LessEqual:
        return LoxType::Bool;
      }
      return LoxType::Unknown;
    };
    auto emitNumberBinary = [&](llvm::Value *leftNumber,
                                llvm::Value *rightNumber) -> llvm::Value * {
      switch (instruction.binaryOp) {
      case BinaryOp::Add:
        return fromDouble(builder_.CreateFAdd(leftNumber, rightNumber, "add"));
      case BinaryOp::Subtract:
        return fromDouble(builder_.CreateFSub(leftNumber, rightNumber, "sub"));
      case BinaryOp::Multiply:
        return fromDouble(builder_.CreateFMul(leftNumber, rightNumber, "mul"));
      case BinaryOp::Divide:
        return fromDouble(builder_.CreateFDiv(leftNumber, rightNumber, "div"));
      case BinaryOp::Equal:
        return boolValue(
            builder_.CreateFCmpOEQ(leftNumber, rightNumber, "eq"));
      case BinaryOp::NotEqual:
        return boolValue(
            builder_.CreateFCmpUNE(leftNumber, rightNumber, "neq"));
      case BinaryOp::Greater:
        return boolValue(
            builder_.CreateFCmpOGT(leftNumber, rightNumber, "gt"));
      case BinaryOp::GreaterEqual:
        return boolValue(
            builder_.CreateFCmpOGE(leftNumber, rightNumber, "ge"));
      case BinaryOp::Less:
        return boolValue(
            builder_.CreateFCmpOLT(leftNumber, rightNumber, "lt"));
      case BinaryOp::LessEqual:
        return boolValue(
            builder_.CreateFCmpOLE(leftNumber, rightNumber, "le"));
      }
      return nilValue();
    };

    if ((instruction.binaryOp == BinaryOp::Equal ||
         instruction.binaryOp == BinaryOp::NotEqual) &&
        leftType != LoxType::Unknown && rightType != LoxType::Unknown &&
        !(leftType == LoxType::Number && rightType == LoxType::Number)) {
      llvm::Value *equal = nullptr;
      if (leftType != rightType) {
        equal = builder_.getFalse();
      } else {
        equal = builder_.CreateICmpEQ(left, right, "eq.bits");
      }
      if (instruction.binaryOp == BinaryOp::NotEqual) {
        equal = builder_.CreateNot(equal, "neq.bits");
      }
      bind(instruction, boolValue(equal), LoxType::Bool);
      return std::nullopt;
    }

    if (leftType != LoxType::Number || rightType != LoxType::Number) {
      const char *helper = nullptr;
      LoxType resultType = LoxType::Unknown;
      switch (instruction.binaryOp) {
      case BinaryOp::Add:
        helper = "elx_add_values";
        break;
      case BinaryOp::Subtract:
        helper = "elx_subtract_values";
        break;
      case BinaryOp::Multiply:
        helper = "elx_multiply_values";
        break;
      case BinaryOp::Divide:
        helper = "elx_divide_values";
        break;
      case BinaryOp::Equal:
        helper = "elx_equal_values";
        resultType = LoxType::Bool;
        break;
      case BinaryOp::NotEqual:
        helper = "elx_not_equal_values";
        resultType = LoxType::Bool;
        break;
      case BinaryOp::Greater:
        helper = "elx_greater_values";
        resultType = LoxType::Bool;
        break;
      case BinaryOp::GreaterEqual:
        helper = "elx_greater_equal_values";
        resultType = LoxType::Bool;
        break;
      case BinaryOp::Less:
        helper = "elx_less_values";
        resultType = LoxType::Bool;
        break;
      case BinaryOp::LessEqual:
        helper = "elx_less_equal_values";
        resultType = LoxType::Bool;
        break;
      }
      auto *operation = runtime(helper);
      if (!operation) {
        return unsupported(instruction, "missing dynamic binary helper");
      }

      if (couldBeNumber(leftType) && couldBeNumber(rightType)) {
        llvm::Value *leftIsNumber =
            leftType == LoxType::Number ? builder_.getTrue()
                                        : isNumberValue(left);
        llvm::Value *rightIsNumber =
            rightType == LoxType::Number ? builder_.getTrue()
                                         : isNumberValue(right);
        auto *bothNumbers =
            builder_.CreateAnd(leftIsNumber, rightIsNumber,
                               "dynamic.binary.both.numbers");

        auto *numberBlock =
            llvm::BasicBlock::Create(ctx_, "dynamic.binary.number", function_);
        auto *fallbackBlock = llvm::BasicBlock::Create(
            ctx_, "dynamic.binary.generic", function_);
        auto *doneBlock =
            llvm::BasicBlock::Create(ctx_, "dynamic.binary.done", function_);
        builder_.CreateCondBr(bothNumbers, numberBlock, fallbackBlock);

        builder_.SetInsertPoint(numberBlock);
        auto *fastResult =
            emitNumberBinary(asDouble(left), asDouble(right));
        builder_.CreateBr(doneBlock);
        numberBlock = builder_.GetInsertBlock();

        builder_.SetInsertPoint(fallbackBlock);
        auto *genericResult =
            builder_.CreateCall(operation, {left, right}, "dynamic.binary");
        guardRuntimeError();
        builder_.CreateBr(doneBlock);
        fallbackBlock = builder_.GetInsertBlock();

        builder_.SetInsertPoint(doneBlock);
        auto *result =
            builder_.CreatePHI(valueTy(), 2, "dynamic.binary.result");
        result->addIncoming(fastResult, numberBlock);
        result->addIncoming(genericResult, fallbackBlock);
        bind(instruction, result, resultTypeForDynamicNumberOp());
        return std::nullopt;
      }

      auto *result =
          builder_.CreateCall(operation, {left, right}, "dynamic.binary");
      guardRuntimeError();
      bind(instruction, result, resultType);
      return std::nullopt;
    }

    auto *leftNumber = asDouble(left);
    auto *rightNumber = asDouble(right);
    llvm::Value *result = nullptr;
    LoxType resultType = LoxType::Number;

    switch (instruction.binaryOp) {
    case BinaryOp::Add:
    case BinaryOp::Subtract:
    case BinaryOp::Multiply:
    case BinaryOp::Divide:
      result = emitNumberBinary(leftNumber, rightNumber);
      break;
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual:
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
      result = emitNumberBinary(leftNumber, rightNumber);
      resultType = LoxType::Bool;
      break;
    }

    bind(instruction, result, resultType);
    return std::nullopt;
  }

  std::optional<std::string> emitUnary(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 1)) {
      return failure;
    }
    auto *operand = lookup(instruction.operands[0]);
    switch (instruction.unaryOp) {
    case UnaryOp::Not:
      bind(instruction,
           boolValue(builder_.CreateNot(truthy(instruction.operands[0]), "not")),
           LoxType::Bool);
      return std::nullopt;
    case UnaryOp::Negate:
      if (typeOf(instruction.operands[0]) != LoxType::Number) {
        auto *negate = runtime("elx_negate_value");
        if (!negate) {
          return unsupported(instruction, "missing elx_negate_value");
        }
        auto *result = builder_.CreateCall(negate, {operand}, "dynamic.neg");
        guardRuntimeError();
        bind(instruction, result, LoxType::Unknown);
        return std::nullopt;
      }
      bind(instruction,
           fromDouble(builder_.CreateFNeg(asDouble(operand), "neg")),
           LoxType::Number);
      return std::nullopt;
    }
    return unsupported(instruction, "unknown unary operation");
  }

  std::optional<std::string> emitIsTruthy(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 1)) {
      return failure;
    }
    ValueId operand = instruction.operands[0];
    if (typeOf(operand) == LoxType::Bool) {
      bind(instruction, lookup(operand), LoxType::Bool);
      return std::nullopt;
    }
    bind(instruction, boolValue(truthy(operand)), LoxType::Bool);
    return std::nullopt;
  }

  std::optional<std::string> emitPrint(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 1)) {
      return failure;
    }
    auto *print = runtime("elx_print");
    if (!print) {
      return unsupported(instruction, "missing elx_print");
    }
    builder_.CreateCall(print, {lookup(instruction.operands[0])});
    return std::nullopt;
  }

  std::optional<std::string> emitJump(const Instruction &instruction) {
    if (!instruction.target.valid() ||
        blocks_.find(instruction.target.id) == blocks_.end()) {
      return unsupported(instruction, "invalid jump target");
    }
    builder_.CreateBr(blocks_.at(instruction.target.id));
    return std::nullopt;
  }

  std::optional<std::string> emitBranch(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 1)) {
      return failure;
    }
    if (!instruction.target.valid() || !instruction.falseTarget.valid() ||
        blocks_.find(instruction.target.id) == blocks_.end() ||
        blocks_.find(instruction.falseTarget.id) == blocks_.end()) {
      return unsupported(instruction, "invalid branch target");
    }
    builder_.CreateCondBr(truthy(instruction.operands[0]),
                          blocks_.at(instruction.target.id),
                          blocks_.at(instruction.falseTarget.id));
    return std::nullopt;
  }

  std::optional<std::string> emitReturn(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 1)) {
      return failure;
    }
    builder_.CreateRet(lookup(instruction.operands[0]));
    return std::nullopt;
  }
};

} // namespace

std::optional<std::string> emitLoxIRModuleToLLVM(const LoxModule &loxModule,
                                                 llvm::Module &module) {
  Emitter emitter(module);
  return emitter.emit(loxModule);
}

} // namespace eloxir::loxir
