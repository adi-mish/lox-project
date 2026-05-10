#include "LoxIR.h"

#include <algorithm>

namespace eloxir::loxir {

BasicBlock::BasicBlock(BlockId id, std::string name)
    : id_(id), name_(std::move(name)) {}

Instruction &BasicBlock::append(Instruction instruction) {
  instructions_.push_back(std::move(instruction));
  return instructions_.back();
}

bool BasicBlock::hasTerminator() const {
  return !instructions_.empty() && isTerminator(instructions_.back().kind);
}

LoxFunction::LoxFunction(std::string name)
    : name_(std::move(name)), displayName_(name_) {}

Parameter &LoxFunction::addParameter(std::string name, LoxType type) {
  Parameter parameter{std::move(name), makeValue(), type};
  parameters_.push_back(std::move(parameter));
  return parameters_.back();
}

Upvalue &LoxFunction::addUpvalue(Upvalue upvalue) {
  upvalues_.push_back(std::move(upvalue));
  return upvalues_.back();
}

BasicBlock &LoxFunction::addBlock(std::string name) {
  BlockId id{static_cast<uint32_t>(blocks_.size())};
  blocks_.emplace_back(id, std::move(name));
  return blocks_.back();
}

BasicBlock *LoxFunction::findBlock(BlockId id) {
  if (!id.valid() || id.id >= blocks_.size()) {
    return nullptr;
  }
  return &blocks_[id.id];
}

const BasicBlock *LoxFunction::findBlock(BlockId id) const {
  if (!id.valid() || id.id >= blocks_.size()) {
    return nullptr;
  }
  return &blocks_[id.id];
}

ValueId LoxFunction::makeValue() { return ValueId{nextValueId_++}; }

LoxModule::LoxModule(std::string name) : name_(std::move(name)) {}

LoxFunction &LoxModule::addFunction(std::string name) {
  functions_.emplace_back(std::move(name));
  return functions_.back();
}

LoxFunction *LoxModule::findFunction(const std::string &name) {
  auto it = std::find_if(functions_.begin(), functions_.end(),
                         [&](const LoxFunction &fn) {
                           return fn.name() == name;
                         });
  return it == functions_.end() ? nullptr : &*it;
}

const LoxFunction *LoxModule::findFunction(const std::string &name) const {
  auto it = std::find_if(functions_.begin(), functions_.end(),
                         [&](const LoxFunction &fn) {
                           return fn.name() == name;
                         });
  return it == functions_.end() ? nullptr : &*it;
}

bool isTerminator(InstructionKind kind) {
  switch (kind) {
  case InstructionKind::Jump:
  case InstructionKind::Branch:
  case InstructionKind::Return:
  case InstructionKind::Unreachable:
    return true;
  default:
    return false;
  }
}

const char *toString(LoxType type) {
  switch (type) {
  case LoxType::Unknown:
    return "unknown";
  case LoxType::Nil:
    return "nil";
  case LoxType::Bool:
    return "bool";
  case LoxType::Number:
    return "number";
  case LoxType::String:
    return "string";
  case LoxType::Object:
    return "object";
  case LoxType::Function:
    return "function";
  case LoxType::Closure:
    return "closure";
  case LoxType::Class:
    return "class";
  case LoxType::Instance:
    return "instance";
  }
  return "unknown";
}

const char *toString(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add:
    return "+";
  case BinaryOp::Subtract:
    return "-";
  case BinaryOp::Multiply:
    return "*";
  case BinaryOp::Divide:
    return "/";
  case BinaryOp::Equal:
    return "==";
  case BinaryOp::NotEqual:
    return "!=";
  case BinaryOp::Greater:
    return ">";
  case BinaryOp::GreaterEqual:
    return ">=";
  case BinaryOp::Less:
    return "<";
  case BinaryOp::LessEqual:
    return "<=";
  }
  return "?";
}

const char *toString(UnaryOp op) {
  switch (op) {
  case UnaryOp::Negate:
    return "-";
  case UnaryOp::Not:
    return "!";
  }
  return "?";
}

const char *toString(UpvalueSourceKind kind) {
  switch (kind) {
  case UpvalueSourceKind::Local:
    return "local";
  case UpvalueSourceKind::Upvalue:
    return "upvalue";
  }
  return "unknown";
}

const char *toString(InstructionKind kind) {
  switch (kind) {
  case InstructionKind::ConstantNil:
    return "const.nil";
  case InstructionKind::ConstantBool:
    return "const.bool";
  case InstructionKind::ConstantNumber:
    return "const.number";
  case InstructionKind::ConstantString:
    return "const.string";
  case InstructionKind::LoadLocal:
    return "load.local";
  case InstructionKind::StoreLocal:
    return "store.local";
  case InstructionKind::LoadGlobal:
    return "load.global";
  case InstructionKind::StoreGlobal:
    return "store.global";
  case InstructionKind::LoadUpvalue:
    return "load.upvalue";
  case InstructionKind::StoreUpvalue:
    return "store.upvalue";
  case InstructionKind::Phi:
    return "phi";
  case InstructionKind::Binary:
    return "binary";
  case InstructionKind::Unary:
    return "unary";
  case InstructionKind::IsTruthy:
    return "is.truthy";
  case InstructionKind::Print:
    return "print";
  case InstructionKind::DefineFunction:
    return "function";
  case InstructionKind::Call:
    return "call";
  case InstructionKind::PreparePropertyCall:
    return "prepare.property.call";
  case InstructionKind::CallPreparedProperty:
    return "call.prepared.property";
  case InstructionKind::GetProperty:
    return "get.property";
  case InstructionKind::SetProperty:
    return "set.property";
  case InstructionKind::DefineClass:
    return "class";
  case InstructionKind::DefineMethod:
    return "method";
  case InstructionKind::BindSuper:
    return "super.bind";
  case InstructionKind::CloseUpvalues:
    return "close.upvalues";
  case InstructionKind::Jump:
    return "jump";
  case InstructionKind::Branch:
    return "branch";
  case InstructionKind::Return:
    return "return";
  case InstructionKind::Unreachable:
    return "unreachable";
  }
  return "unknown";
}

} // namespace eloxir::loxir
