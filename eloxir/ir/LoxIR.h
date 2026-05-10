#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eloxir::loxir {

struct SourceLocation {
  int line = 0;
};

struct ValueId {
  uint32_t id = UINT32_MAX;

  bool valid() const { return id != UINT32_MAX; }
  friend bool operator==(ValueId left, ValueId right) {
    return left.id == right.id;
  }
  friend bool operator!=(ValueId left, ValueId right) {
    return !(left == right);
  }
};

struct BlockId {
  uint32_t id = UINT32_MAX;

  bool valid() const { return id != UINT32_MAX; }
  friend bool operator==(BlockId left, BlockId right) {
    return left.id == right.id;
  }
};

enum class LoxType {
  Unknown,
  Nil,
  Bool,
  Number,
  String,
  Object,
  Function,
  Closure,
  Class,
  Instance,
};

enum class BinaryOp {
  Add,
  Subtract,
  Multiply,
  Divide,
  Equal,
  NotEqual,
  Greater,
  GreaterEqual,
  Less,
  LessEqual,
};

enum class UnaryOp {
  Negate,
  Not,
};

enum class UpvalueSourceKind {
  Local,
  Upvalue,
};

struct Upvalue {
  std::string name;
  UpvalueSourceKind source = UpvalueSourceKind::Local;
  std::string sourceSymbol;
  uint32_t sourceIndex = 0;
};

enum class InstructionKind {
  ConstantNil,
  ConstantBool,
  ConstantNumber,
  ConstantString,
  LoadLocal,
  StoreLocal,
  LoadGlobal,
  StoreGlobal,
  LoadUpvalue,
  StoreUpvalue,
  Phi,
  Binary,
  Unary,
  IsTruthy,
  Print,
  DefineFunction,
  Call,
  DirectCall,
  PreparePropertyCall,
  CallPreparedProperty,
  GetProperty,
  SetProperty,
  DefineClass,
  DefineMethod,
  BindSuper,
  CloseUpvalues,
  Jump,
  Branch,
  Return,
  Unreachable,
};

struct Instruction {
  InstructionKind kind = InstructionKind::Unreachable;
  SourceLocation source;
  std::optional<ValueId> result;
  std::optional<ValueId> auxResult;
  LoxType resultType = LoxType::Unknown;
  std::vector<ValueId> operands;
  std::vector<ValueId> arguments;
  std::string symbol;
  double numberValue = 0.0;
  bool boolValue = false;
  bool declaresSymbol = false;
  BinaryOp binaryOp = BinaryOp::Add;
  UnaryOp unaryOp = UnaryOp::Not;
  BlockId target;
  BlockId falseTarget;
};

struct Parameter {
  std::string name;
  ValueId value;
  LoxType type = LoxType::Unknown;
};

class BasicBlock {
public:
  explicit BasicBlock(BlockId id, std::string name);

  BlockId id() const { return id_; }
  const std::string &name() const { return name_; }
  std::vector<Instruction> &instructions() { return instructions_; }
  const std::vector<Instruction> &instructions() const { return instructions_; }

  Instruction &append(Instruction instruction);
  bool hasTerminator() const;

private:
  BlockId id_;
  std::string name_;
  std::vector<Instruction> instructions_;
};

class LoxFunction {
public:
  explicit LoxFunction(std::string name);

  const std::string &name() const { return name_; }
  const std::string &displayName() const { return displayName_; }
  void setDisplayName(std::string name) { displayName_ = std::move(name); }
  int arity() const { return arity_; }
  void setArity(int arity) { arity_ = arity; }
  bool isInitializer() const { return isInitializer_; }
  void setInitializer(bool isInitializer) { isInitializer_ = isInitializer; }
  bool isMethod() const { return isMethod_; }
  void setMethod(bool isMethod) { isMethod_ = isMethod; }
  std::vector<Parameter> &parameters() { return parameters_; }
  const std::vector<Parameter> &parameters() const { return parameters_; }
  std::vector<Upvalue> &upvalues() { return upvalues_; }
  const std::vector<Upvalue> &upvalues() const { return upvalues_; }
  std::vector<BasicBlock> &blocks() { return blocks_; }
  const std::vector<BasicBlock> &blocks() const { return blocks_; }

  Parameter &addParameter(std::string name, LoxType type = LoxType::Unknown);
  Upvalue &addUpvalue(Upvalue upvalue);
  BasicBlock &addBlock(std::string name);
  BasicBlock *findBlock(BlockId id);
  const BasicBlock *findBlock(BlockId id) const;
  ValueId makeValue();

private:
  std::string name_;
  std::string displayName_;
  int arity_ = 0;
  bool isInitializer_ = false;
  bool isMethod_ = false;
  std::vector<Parameter> parameters_;
  std::vector<Upvalue> upvalues_;
  std::vector<BasicBlock> blocks_;
  uint32_t nextValueId_ = 0;
};

class LoxModule {
public:
  explicit LoxModule(std::string name);

  const std::string &name() const { return name_; }
  std::deque<LoxFunction> &functions() { return functions_; }
  const std::deque<LoxFunction> &functions() const { return functions_; }

  LoxFunction &addFunction(std::string name);
  LoxFunction *findFunction(const std::string &name);
  const LoxFunction *findFunction(const std::string &name) const;

private:
  std::string name_;
  std::deque<LoxFunction> functions_;
};

bool isTerminator(InstructionKind kind);
const char *toString(LoxType type);
const char *toString(BinaryOp op);
const char *toString(UnaryOp op);
const char *toString(UpvalueSourceKind kind);
const char *toString(InstructionKind kind);

} // namespace eloxir::loxir
