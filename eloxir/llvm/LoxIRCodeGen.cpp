#include "LoxIRCodeGen.h"

#include "BuiltinsIR.h"
#include "../runtime/RuntimeAPI.h"
#include "../runtime/Value.h"

#include <cstdint>
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

  llvm::IntegerType *valueTy() { return llvm::Type::getInt64Ty(ctx_); }
  llvm::IntegerType *i32Ty() { return llvm::Type::getInt32Ty(ctx_); }
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

  llvm::Value *nullValuePtr() {
    return llvm::ConstantPointerNull::get(valuePtrTy());
  }

  void guardRuntimeError() {
    auto *hasError = runtime("elx_has_runtime_error");
    if (!hasError || !function_) {
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
        builder_.CreateCall(hasError, {}, "has.runtime.error"), constantI32(0),
        "has.runtime.error.bool");
    builder_.CreateCondBr(errored, errorBlock, continueBlock);

    builder_.SetInsertPoint(errorBlock);
    builder_.CreateRet(nilValue());

    builder_.SetInsertPoint(continueBlock);
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
    capturedLocals_ = capturedLocalSymbols(function);

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
    auto *intern = runtime("elx_intern_string");
    if (!intern) {
      return unsupported(instruction, "missing elx_intern_string");
    }
    auto *text = stringPtr(instruction.symbol, "lox.str");
    auto *value = builder_.CreateCall(
        intern, {text, constantI32(static_cast<int>(instruction.symbol.size()))},
        "str");
    bind(instruction, value, LoxType::String);
    return std::nullopt;
  }

  llvm::Value *emitInternedName(const std::string &text,
                                const std::string &name) {
    auto *intern = runtime("elx_intern_string");
    if (!intern) {
      return nilValue();
    }
    return builder_.CreateCall(
        intern, {stringPtr(text, name), constantI32(static_cast<int>(text.size()))},
        name + ".interned");
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
      return std::nullopt;
    }
    auto *slot = localSlot(instruction.symbol);
    if (!slot) {
      return unsupported(instruction, "missing elx_allocate_value_slot");
    }
    builder_.CreateStore(lookup(instruction.operands[0]), slot);
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
    if (functionIt == functions_.end() || loxIt == loxFunctions_.end() ||
        !allocateFunction) {
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

    if (target.upvalues().empty() && !target.isMethod()) {
      bind(instruction, functionObject, LoxType::Function);
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
    auto *args = emitArgumentArray(instruction.arguments, "call.args");
    auto *count = constantI32(static_cast<int>(instruction.arguments.size()));
    auto *result =
        builder_.CreateCall(call, {lookup(instruction.operands[0]), args, count},
                            "call");
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
    auto *kind = builder_.CreateCall(
        prepare,
        {lookup(instruction.operands[0]), name,
         builder_.CreatePointerCast(cache, callCachePtrTy()), targetSlot},
        "property.call.kind");
    guardRuntimeError();
    auto *target =
        builder_.CreateLoad(valueTy(), targetSlot, "property.call.target");
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
    auto *args =
        emitArgumentArray(instruction.arguments, "prepared.property.args");
    auto *count = constantI32(static_cast<int>(instruction.arguments.size()));
    auto *result = builder_.CreateCall(
        call,
        {lookup(instruction.operands[1]), lookup(instruction.operands[0]),
         lookup(instruction.operands[2]), args, count},
        "prepared.property.call");
    guardRuntimeError();
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
    auto *result = builder_.CreateCall(
        get,
        {lookup(instruction.operands[0]), name,
         builder_.CreatePointerCast(cache, propertyCachePtrTy()),
         constantI32(static_cast<int>(eloxir::PROPERTY_CACHE_MAX_SIZE))},
        "property");
    guardRuntimeError();
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
    auto *result = builder_.CreateCall(
        set,
        {lookup(instruction.operands[0]), name, lookup(instruction.operands[1]),
         builder_.CreatePointerCast(cache, propertyCachePtrTy()),
         constantI32(static_cast<int>(eloxir::PROPERTY_CACHE_MAX_SIZE))},
        "set.property");
    guardRuntimeError();
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
      result = fromDouble(builder_.CreateFAdd(leftNumber, rightNumber, "add"));
      break;
    case BinaryOp::Subtract:
      result = fromDouble(builder_.CreateFSub(leftNumber, rightNumber, "sub"));
      break;
    case BinaryOp::Multiply:
      result = fromDouble(builder_.CreateFMul(leftNumber, rightNumber, "mul"));
      break;
    case BinaryOp::Divide:
      result = fromDouble(builder_.CreateFDiv(leftNumber, rightNumber, "div"));
      break;
    case BinaryOp::Equal:
      result = boolValue(builder_.CreateFCmpOEQ(leftNumber, rightNumber, "eq"));
      resultType = LoxType::Bool;
      break;
    case BinaryOp::NotEqual:
      result = boolValue(builder_.CreateFCmpUNE(leftNumber, rightNumber, "neq"));
      resultType = LoxType::Bool;
      break;
    case BinaryOp::Greater:
      result = boolValue(builder_.CreateFCmpOGT(leftNumber, rightNumber, "gt"));
      resultType = LoxType::Bool;
      break;
    case BinaryOp::GreaterEqual:
      result = boolValue(builder_.CreateFCmpOGE(leftNumber, rightNumber, "ge"));
      resultType = LoxType::Bool;
      break;
    case BinaryOp::Less:
      result = boolValue(builder_.CreateFCmpOLT(leftNumber, rightNumber, "lt"));
      resultType = LoxType::Bool;
      break;
    case BinaryOp::LessEqual:
      result = boolValue(builder_.CreateFCmpOLE(leftNumber, rightNumber, "le"));
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
      bind(instruction, boolValue(builder_.CreateNot(truthy(operand), "not")),
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
    bind(instruction, boolValue(truthy(lookup(instruction.operands[0]))),
         LoxType::Bool);
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
    builder_.CreateCondBr(truthy(lookup(instruction.operands[0])),
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
