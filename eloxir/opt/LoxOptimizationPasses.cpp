#include "LoxOptimizationPasses.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace eloxir::loxir {
namespace {

struct Constant {
  using Payload = std::variant<std::monostate, bool, double, std::string>;

  LoxType type = LoxType::Unknown;
  Payload payload;
};

struct ConstantTable {
  std::unordered_map<uint32_t, Constant> values;

  void erase(std::optional<ValueId> value) {
    if (value && value->valid()) {
      values.erase(value->id);
    }
  }

  void set(ValueId value, Constant constant) {
    if (value.valid()) {
      values[value.id] = std::move(constant);
    }
  }

  const Constant *find(ValueId value) const {
    if (!value.valid()) {
      return nullptr;
    }
    auto it = values.find(value.id);
    return it == values.end() ? nullptr : &it->second;
  }
};

bool asNumber(const Constant &constant, double &out) {
  if (constant.type != LoxType::Number) {
    return false;
  }
  if (const auto *number = std::get_if<double>(&constant.payload)) {
    out = *number;
    return true;
  }
  return false;
}

bool loxTruthy(const Constant &constant) {
  if (constant.type == LoxType::Nil) {
    return false;
  }
  if (constant.type == LoxType::Bool) {
    if (const auto *value = std::get_if<bool>(&constant.payload)) {
      return *value;
    }
  }
  return true;
}

std::optional<bool> equalConstants(const Constant &left,
                                   const Constant &right) {
  if (left.type != right.type) {
    return false;
  }
  switch (left.type) {
  case LoxType::Nil:
    return true;
  case LoxType::Bool: {
    const auto *leftValue = std::get_if<bool>(&left.payload);
    const auto *rightValue = std::get_if<bool>(&right.payload);
    if (!leftValue || !rightValue) {
      return std::nullopt;
    }
    return *leftValue == *rightValue;
  }
  case LoxType::Number: {
    const auto *leftValue = std::get_if<double>(&left.payload);
    const auto *rightValue = std::get_if<double>(&right.payload);
    if (!leftValue || !rightValue) {
      return std::nullopt;
    }
    return *leftValue == *rightValue;
  }
  case LoxType::String: {
    const auto *leftValue = std::get_if<std::string>(&left.payload);
    const auto *rightValue = std::get_if<std::string>(&right.payload);
    if (!leftValue || !rightValue) {
      return std::nullopt;
    }
    return *leftValue == *rightValue;
  }
  default:
    return std::nullopt;
  }
}

void replaceWithNil(Instruction &instruction) {
  instruction.kind = InstructionKind::ConstantNil;
  instruction.resultType = LoxType::Nil;
  instruction.operands.clear();
  instruction.arguments.clear();
  instruction.symbol.clear();
}

void replaceWithBool(Instruction &instruction, bool value) {
  instruction.kind = InstructionKind::ConstantBool;
  instruction.resultType = LoxType::Bool;
  instruction.boolValue = value;
  instruction.operands.clear();
  instruction.arguments.clear();
  instruction.symbol.clear();
}

void replaceWithNumber(Instruction &instruction, double value) {
  instruction.kind = InstructionKind::ConstantNumber;
  instruction.resultType = LoxType::Number;
  instruction.numberValue = value;
  instruction.operands.clear();
  instruction.arguments.clear();
  instruction.symbol.clear();
}

void replaceWithString(Instruction &instruction, const std::string &value) {
  instruction.kind = InstructionKind::ConstantString;
  instruction.resultType = LoxType::String;
  instruction.symbol = value;
  instruction.operands.clear();
  instruction.arguments.clear();
}

std::optional<Constant> foldBinary(BinaryOp op, const Constant &left,
                                   const Constant &right) {
  if (op == BinaryOp::Equal || op == BinaryOp::NotEqual) {
    auto equal = equalConstants(left, right);
    if (!equal) {
      return std::nullopt;
    }
    bool result = op == BinaryOp::Equal ? *equal : !*equal;
    return Constant{LoxType::Bool, result};
  }

  double leftNumber = 0.0;
  double rightNumber = 0.0;
  if (!asNumber(left, leftNumber) || !asNumber(right, rightNumber)) {
    return std::nullopt;
  }

  switch (op) {
  case BinaryOp::Add:
    return Constant{LoxType::Number, leftNumber + rightNumber};
  case BinaryOp::Subtract:
    return Constant{LoxType::Number, leftNumber - rightNumber};
  case BinaryOp::Multiply:
    return Constant{LoxType::Number, leftNumber * rightNumber};
  case BinaryOp::Divide:
    return Constant{LoxType::Number, leftNumber / rightNumber};
  case BinaryOp::Greater:
    return Constant{LoxType::Bool, leftNumber > rightNumber};
  case BinaryOp::GreaterEqual:
    return Constant{LoxType::Bool, leftNumber >= rightNumber};
  case BinaryOp::Less:
    return Constant{LoxType::Bool, leftNumber < rightNumber};
  case BinaryOp::LessEqual:
    return Constant{LoxType::Bool, leftNumber <= rightNumber};
  case BinaryOp::Equal:
  case BinaryOp::NotEqual:
    break;
  }
  return std::nullopt;
}

std::optional<Constant> foldUnary(UnaryOp op, const Constant &operand) {
  switch (op) {
  case UnaryOp::Negate: {
    double number = 0.0;
    if (!asNumber(operand, number)) {
      return std::nullopt;
    }
    return Constant{LoxType::Number, -number};
  }
  case UnaryOp::Not:
    return Constant{LoxType::Bool, !loxTruthy(operand)};
  }
  return std::nullopt;
}

void replaceWithConstant(Instruction &instruction, const Constant &constant) {
  switch (constant.type) {
  case LoxType::Nil:
    replaceWithNil(instruction);
    break;
  case LoxType::Bool:
    replaceWithBool(instruction, std::get<bool>(constant.payload));
    break;
  case LoxType::Number:
    replaceWithNumber(instruction, std::get<double>(constant.payload));
    break;
  case LoxType::String:
    replaceWithString(instruction, std::get<std::string>(constant.payload));
    break;
  default:
    break;
  }
}

std::optional<Constant> instructionConstant(const Instruction &instruction) {
  switch (instruction.kind) {
  case InstructionKind::ConstantNil:
    return Constant{LoxType::Nil, std::monostate{}};
  case InstructionKind::ConstantBool:
    return Constant{LoxType::Bool, instruction.boolValue};
  case InstructionKind::ConstantNumber:
    return Constant{LoxType::Number, instruction.numberValue};
  case InstructionKind::ConstantString:
    return Constant{LoxType::String, instruction.symbol};
  default:
    return std::nullopt;
  }
}

bool foldInstruction(Instruction &instruction, ConstantTable &constants) {
  if (!instruction.result) {
    return false;
  }

  constants.erase(instruction.result);
  if (auto constant = instructionConstant(instruction)) {
    constants.set(*instruction.result, *constant);
    return false;
  }

  std::optional<Constant> folded;
  switch (instruction.kind) {
  case InstructionKind::Binary:
    if (instruction.operands.size() == 2) {
      const auto *left = constants.find(instruction.operands[0]);
      const auto *right = constants.find(instruction.operands[1]);
      if (left && right) {
        folded = foldBinary(instruction.binaryOp, *left, *right);
      }
    }
    break;
  case InstructionKind::Unary:
    if (instruction.operands.size() == 1) {
      const auto *operand = constants.find(instruction.operands[0]);
      if (operand) {
        folded = foldUnary(instruction.unaryOp, *operand);
      }
    }
    break;
  case InstructionKind::IsTruthy:
    if (instruction.operands.size() == 1) {
      const auto *operand = constants.find(instruction.operands[0]);
      if (operand) {
        folded = Constant{LoxType::Bool, loxTruthy(*operand)};
      }
    }
    break;
  default:
    break;
  }

  if (!folded) {
    return false;
  }

  replaceWithConstant(instruction, *folded);
  constants.set(*instruction.result, *folded);
  return true;
}

bool isPure(const Instruction &instruction) {
  switch (instruction.kind) {
  case InstructionKind::ConstantNil:
  case InstructionKind::ConstantBool:
  case InstructionKind::ConstantNumber:
  case InstructionKind::ConstantString:
  case InstructionKind::Phi:
  case InstructionKind::Binary:
  case InstructionKind::Unary:
  case InstructionKind::IsTruthy:
    return true;
  default:
    return false;
  }
}

LoxType valueType(ValueId value,
                  const std::unordered_map<uint32_t, LoxType> &types) {
  auto it = types.find(value.id);
  return it == types.end() ? LoxType::Unknown : it->second;
}

bool setResultType(Instruction &instruction, LoxType type) {
  if (!instruction.result || instruction.resultType == type) {
    return false;
  }
  instruction.resultType = type;
  return true;
}

bool mergeGlobalType(std::unordered_map<std::string, LoxType> &globalTypes,
                     const std::string &name, LoxType type) {
  if (type == LoxType::Unknown) {
    return false;
  }
  auto [it, inserted] = globalTypes.emplace(name, type);
  if (inserted || it->second == type) {
    return inserted;
  }
  if (it->second != LoxType::Unknown) {
    it->second = LoxType::Unknown;
    return true;
  }
  return false;
}

LoxType inferResultType(
    const Instruction &instruction,
    const std::unordered_map<uint32_t, LoxType> &valueTypes,
    const std::unordered_map<std::string, LoxType> &globalTypes) {
  switch (instruction.kind) {
  case InstructionKind::ConstantNil:
    return LoxType::Nil;
  case InstructionKind::ConstantBool:
    return LoxType::Bool;
  case InstructionKind::ConstantNumber:
    return LoxType::Number;
  case InstructionKind::ConstantString:
    return LoxType::String;
  case InstructionKind::LoadGlobal: {
    auto it = globalTypes.find(instruction.symbol);
    return it == globalTypes.end() ? LoxType::Unknown : it->second;
  }
  case InstructionKind::Binary:
    if (instruction.binaryOp == BinaryOp::Equal ||
        instruction.binaryOp == BinaryOp::NotEqual) {
      return LoxType::Bool;
    }
    if (instruction.operands.size() != 2) {
      return LoxType::Unknown;
    }
    if (valueType(instruction.operands[0], valueTypes) != LoxType::Number ||
        valueType(instruction.operands[1], valueTypes) != LoxType::Number) {
      return LoxType::Unknown;
    }
    switch (instruction.binaryOp) {
    case BinaryOp::Add:
    case BinaryOp::Subtract:
    case BinaryOp::Multiply:
    case BinaryOp::Divide:
      return LoxType::Number;
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual:
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
      return LoxType::Bool;
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
      return LoxType::Bool;
    }
    return LoxType::Unknown;
  case InstructionKind::Unary:
    if (instruction.unaryOp == UnaryOp::Not) {
      return LoxType::Bool;
    }
    if (instruction.operands.size() == 1 &&
        valueType(instruction.operands[0], valueTypes) == LoxType::Number) {
      return LoxType::Number;
    }
    return LoxType::Unknown;
  case InstructionKind::IsTruthy:
    return LoxType::Bool;
  default:
    return instruction.resultType;
  }
}

void collectUses(const Instruction &instruction,
                 std::unordered_set<uint32_t> &usedValues) {
  for (ValueId value : instruction.operands) {
    if (value.valid()) {
      usedValues.insert(value.id);
    }
  }
  for (ValueId value : instruction.arguments) {
    if (value.valid()) {
      usedValues.insert(value.id);
    }
  }
}

class ConstantFoldingPass final : public LoxPass {
public:
  std::string name() const override { return "constant-folding"; }

  bool run(LoxModule &module) override {
    bool changed = false;
    for (auto &function : module.functions()) {
      for (auto &block : function.blocks()) {
        ConstantTable constants;
        for (auto &instruction : block.instructions()) {
          changed |= foldInstruction(instruction, constants);
        }
      }
    }
    return changed;
  }
};

class TypePropagationPass final : public LoxPass {
public:
  std::string name() const override { return "type-propagation"; }

  bool run(LoxModule &module) override {
    bool changed = false;
    for (auto &function : module.functions()) {
      std::unordered_map<std::string, LoxType> globalTypes;
      bool functionChanged = false;
      do {
        functionChanged = false;
        std::unordered_map<uint32_t, LoxType> valueTypes;
        for (const auto &parameter : function.parameters()) {
          valueTypes[parameter.value.id] = parameter.type;
        }

        for (auto &block : function.blocks()) {
          for (auto &instruction : block.instructions()) {
            LoxType inferred =
                inferResultType(instruction, valueTypes, globalTypes);
            functionChanged |= setResultType(instruction, inferred);
            if (instruction.result) {
              valueTypes[instruction.result->id] = instruction.resultType;
            }
            if (instruction.kind == InstructionKind::StoreGlobal &&
                instruction.operands.size() == 1) {
              functionChanged |= mergeGlobalType(
                  globalTypes, instruction.symbol,
                  valueType(instruction.operands[0], valueTypes));
            }
          }
        }
        changed |= functionChanged;
      } while (functionChanged);
    }
    return changed;
  }
};

class DeadCodeEliminationPass final : public LoxPass {
public:
  std::string name() const override { return "dead-code-elimination"; }

  bool run(LoxModule &module) override {
    bool changed = false;
    for (auto &function : module.functions()) {
      bool functionChanged = false;
      do {
        functionChanged = false;
        std::unordered_set<uint32_t> usedValues;
        for (const auto &parameter : function.parameters()) {
          if (parameter.value.valid()) {
            usedValues.insert(parameter.value.id);
          }
        }
        for (const auto &block : function.blocks()) {
          for (const auto &instruction : block.instructions()) {
            collectUses(instruction, usedValues);
          }
        }

        for (auto &block : function.blocks()) {
          auto &instructions = block.instructions();
          const auto oldSize = instructions.size();
          instructions.erase(
              std::remove_if(instructions.begin(), instructions.end(),
                             [&](const Instruction &instruction) {
                               return instruction.result &&
                                      isPure(instruction) &&
                                      usedValues.find(instruction.result->id) ==
                                          usedValues.end();
                             }),
              instructions.end());
          functionChanged |= instructions.size() != oldSize;
        }
        changed |= functionChanged;
      } while (functionChanged);
    }
    return changed;
  }
};

} // namespace

std::unique_ptr<LoxPass> createConstantFoldingPass() {
  return std::make_unique<ConstantFoldingPass>();
}

std::unique_ptr<LoxPass> createTypePropagationPass() {
  return std::make_unique<TypePropagationPass>();
}

std::unique_ptr<LoxPass> createDeadCodeEliminationPass() {
  return std::make_unique<DeadCodeEliminationPass>();
}

LoxPassManager createDefaultLoxPassPipeline() {
  LoxPassManager manager;
  manager.add(createTypePropagationPass());
  manager.add(createConstantFoldingPass());
  manager.add(createTypePropagationPass());
  manager.add(createDeadCodeEliminationPass());
  return manager;
}

} // namespace eloxir::loxir
