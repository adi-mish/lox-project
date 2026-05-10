#include "LoxIRCodeGen.h"

#include "../codegen/BuiltinsIR.h"
#include "../runtime/Value.h"

#include <cstdint>
#include <cstring>
#include <sstream>
#include <unordered_map>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
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
    if (loxModule.functions().size() != 1 ||
        loxModule.functions().front().name() != "main") {
      return "module contains functions or methods outside the initial LoxIR "
             "backend subset";
    }

    return emitFunction(loxModule.functions().front());
  }

private:
  llvm::Module &module_;
  llvm::LLVMContext &ctx_;
  llvm::IRBuilder<> builder_;
  llvm::Function *function_ = nullptr;
  std::unordered_map<uint32_t, llvm::BasicBlock *> blocks_;
  std::unordered_map<uint32_t, llvm::Value *> values_;
  std::unordered_map<uint32_t, LoxType> types_;
  std::unordered_map<std::string, llvm::AllocaInst *> locals_;

  llvm::IntegerType *valueTy() { return llvm::Type::getInt64Ty(ctx_); }
  llvm::IntegerType *i32Ty() { return llvm::Type::getInt32Ty(ctx_); }
  llvm::PointerType *i8PtrTy() {
    return llvm::PointerType::get(llvm::Type::getInt8Ty(ctx_), 0);
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

  llvm::AllocaInst *localSlot(const std::string &name) {
    auto it = locals_.find(name);
    if (it != locals_.end()) {
      return it->second;
    }

    llvm::IRBuilder<> entryBuilder(
        &function_->getEntryBlock(), function_->getEntryBlock().begin());
    auto *slot = entryBuilder.CreateAlloca(valueTy(), nullptr, name);
    entryBuilder.CreateStore(nilValue(), slot);
    locals_[name] = slot;
    return slot;
  }

  llvm::Value *stringPtr(const std::string &text, const std::string &name) {
    return builder_.CreateGlobalStringPtr(text, name);
  }

  std::optional<std::string> emitFunction(const LoxFunction &function) {
    auto *fnTy = llvm::FunctionType::get(valueTy(), {}, false);
    function_ =
        llvm::Function::Create(fnTy, llvm::Function::ExternalLinkage, "main",
                               module_);

    for (const auto &block : function.blocks()) {
      blocks_[block.id().id] =
          llvm::BasicBlock::Create(ctx_, block.name(), function_);
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

  llvm::Value *lookup(ValueId id) const {
    auto it = values_.find(id.id);
    return it == values_.end() ? nullptr : it->second;
  }

  LoxType typeOf(ValueId id) const {
    auto it = types_.find(id.id);
    return it == types_.end() ? LoxType::Unknown : it->second;
  }

  void bind(const Instruction &instruction, llvm::Value *value,
            LoxType type = LoxType::Unknown) {
    if (!instruction.result) {
      return;
    }
    values_[instruction.result->id] = value;
    types_[instruction.result->id] = type;
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
    case InstructionKind::LoadUpvalue:
    case InstructionKind::StoreUpvalue:
    case InstructionKind::Phi:
    case InstructionKind::DefineFunction:
    case InstructionKind::Call:
    case InstructionKind::GetProperty:
    case InstructionKind::SetProperty:
    case InstructionKind::DefineClass:
    case InstructionKind::DefineMethod:
    case InstructionKind::BindSuper:
    case InstructionKind::CloseUpvalues:
      return unsupported(instruction,
                         "not in the initial guarded backend subset");
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
    builder_.CreateStore(lookup(instruction.operands[0]),
                         localSlot(instruction.symbol));
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

  std::optional<std::string> emitBinary(const Instruction &instruction) {
    if (auto failure = requireOperands(instruction, 2)) {
      return failure;
    }
    ValueId leftId = instruction.operands[0];
    ValueId rightId = instruction.operands[1];
    auto *left = lookup(leftId);
    auto *right = lookup(rightId);

    if (typeOf(leftId) != LoxType::Number || typeOf(rightId) != LoxType::Number) {
      if (instruction.binaryOp == BinaryOp::Equal ||
          instruction.binaryOp == BinaryOp::NotEqual) {
        auto *equal = builder_.CreateICmpEQ(left, right, "eq.bits");
        if (instruction.binaryOp == BinaryOp::NotEqual) {
          equal = builder_.CreateNot(equal, "neq.bits");
        }
        bind(instruction, boolValue(equal), LoxType::Bool);
        return std::nullopt;
      }
      return unsupported(instruction, "dynamic numeric/string operation");
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
        return unsupported(instruction, "dynamic negation");
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
