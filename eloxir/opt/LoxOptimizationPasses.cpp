#include "LoxOptimizationPasses.h"

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

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
  case InstructionKind::IsTruthy:
    return true;
  case InstructionKind::Binary:
    return instruction.binaryOp == BinaryOp::Equal ||
           instruction.binaryOp == BinaryOp::NotEqual;
  case InstructionKind::Unary:
    return instruction.unaryOp == UnaryOp::Not;
  default:
    return false;
  }
}

bool isPure(const Instruction &instruction,
            const std::unordered_set<std::string> &declaredGlobals) {
  if (isPure(instruction)) {
    return true;
  }
  switch (instruction.kind) {
  case InstructionKind::LoadLocal:
  case InstructionKind::LoadUpvalue:
    return true;
  case InstructionKind::LoadGlobal:
    return declaredGlobals.find(instruction.symbol) != declaredGlobals.end();
  default:
    return false;
  }
}

std::vector<BlockId> successors(const BasicBlock &block) {
  if (block.instructions().empty()) {
    return {};
  }
  const Instruction &terminator = block.instructions().back();
  switch (terminator.kind) {
  case InstructionKind::Jump:
    return terminator.target.valid() ? std::vector<BlockId>{terminator.target}
                                     : std::vector<BlockId>{};
  case InstructionKind::Branch: {
    std::vector<BlockId> result;
    if (terminator.target.valid()) {
      result.push_back(terminator.target);
    }
    if (terminator.falseTarget.valid() &&
        terminator.falseTarget.id != terminator.target.id) {
      result.push_back(terminator.falseTarget);
    }
    return result;
  }
  default:
    return {};
  }
}

bool sameSet(const std::unordered_set<std::string> &left,
             const std::unordered_set<std::string> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (const auto &value : left) {
    if (right.find(value) == right.end()) {
      return false;
    }
  }
  return true;
}

std::unordered_set<std::string>
intersectSets(const std::vector<std::unordered_set<std::string>> &sets) {
  if (sets.empty()) {
    return {};
  }
  std::unordered_set<std::string> result = sets.front();
  for (size_t index = 1; index < sets.size(); ++index) {
    for (auto it = result.begin(); it != result.end();) {
      if (sets[index].find(*it) == sets[index].end()) {
        it = result.erase(it);
      } else {
        ++it;
      }
    }
  }
  return result;
}

void addDeclaredGlobals(const BasicBlock &block,
                        std::unordered_set<std::string> &globals) {
  for (const Instruction &instruction : block.instructions()) {
    if (instruction.kind == InstructionKind::StoreGlobal &&
        instruction.declaresSymbol && !instruction.symbol.empty()) {
      globals.insert(instruction.symbol);
    }
  }
}

std::unordered_map<uint32_t, std::unordered_set<std::string>>
computeDefiniteGlobals(const LoxFunction &function) {
  std::unordered_map<uint32_t, std::unordered_set<std::string>> in;
  std::unordered_map<uint32_t, std::unordered_set<std::string>> out;
  std::unordered_map<uint32_t, std::vector<uint32_t>> predecessors;
  std::unordered_set<std::string> allDeclared;

  for (const BasicBlock &block : function.blocks()) {
    addDeclaredGlobals(block, allDeclared);
  }

  const uint32_t entryBlock =
      function.blocks().empty() ? UINT32_MAX : function.blocks().front().id().id;

  for (const BasicBlock &block : function.blocks()) {
    if (block.id().id == entryBlock) {
      in[block.id().id] = {};
      out[block.id().id] = {};
    } else {
      in[block.id().id] = allDeclared;
      out[block.id().id] = allDeclared;
    }
    for (BlockId successor : successors(block)) {
      predecessors[successor.id].push_back(block.id().id);
    }
  }

  bool changed = false;
  do {
    changed = false;
    for (const BasicBlock &block : function.blocks()) {
      std::unordered_set<std::string> newIn;
      if (block.id().id != entryBlock) {
        auto predIt = predecessors.find(block.id().id);
        if (predIt != predecessors.end() && !predIt->second.empty()) {
          std::vector<std::unordered_set<std::string>> incoming;
          incoming.reserve(predIt->second.size());
          for (uint32_t pred : predIt->second) {
            incoming.push_back(out[pred]);
          }
          newIn = intersectSets(incoming);
        }
      }

      auto newOut = newIn;
      addDeclaredGlobals(block, newOut);

      if (!sameSet(in[block.id().id], newIn)) {
        in[block.id().id] = std::move(newIn);
        changed = true;
      }
      if (!sameSet(out[block.id().id], newOut)) {
        out[block.id().id] = std::move(newOut);
        changed = true;
      }
    }
  } while (changed);

  return in;
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

bool setParameterType(Parameter &parameter, LoxType type) {
  if (parameter.type == type) {
    return false;
  }
  parameter.type = type;
  return true;
}

bool mergeGlobalType(std::unordered_map<std::string, LoxType> &globalTypes,
                     const std::string &name, LoxType type) {
  auto [it, inserted] = globalTypes.emplace(name, type);
  if (inserted) {
    return true;
  }
  if (it->second == type) {
    return false;
  }
  if (it->second != LoxType::Unknown) {
    it->second = LoxType::Unknown;
    return true;
  }
  return false;
}

bool mergeCandidateType(std::optional<LoxType> &target, LoxType type) {
  if (!target) {
    target = type;
    return true;
  }
  if (*target == type) {
    return false;
  }
  if (*target != LoxType::Unknown) {
    *target = LoxType::Unknown;
    return true;
  }
  return false;
}

LoxType candidateOrUnknown(const std::optional<LoxType> &candidate) {
  return candidate ? *candidate : LoxType::Unknown;
}

std::unordered_map<std::string, LoxType>
collectModuleGlobalTypes(const LoxModule &module) {
  std::unordered_map<std::string, LoxType> globalTypes;
  for (const auto &function : module.functions()) {
    std::unordered_map<uint32_t, LoxType> valueTypes;
    for (const auto &parameter : function.parameters()) {
      valueTypes[parameter.value.id] = parameter.type;
    }
    for (const auto &block : function.blocks()) {
      for (const auto &instruction : block.instructions()) {
        LoxType instructionType = instruction.resultType;
        switch (instruction.kind) {
        case InstructionKind::ConstantNil:
          instructionType = LoxType::Nil;
          break;
        case InstructionKind::ConstantBool:
          instructionType = LoxType::Bool;
          break;
        case InstructionKind::ConstantNumber:
          instructionType = LoxType::Number;
          break;
        case InstructionKind::ConstantString:
          instructionType = LoxType::String;
          break;
        case InstructionKind::DefineFunction:
          instructionType = instruction.resultType == LoxType::Closure
                                ? LoxType::Closure
                                : LoxType::Function;
          break;
        case InstructionKind::DefineClass:
          instructionType = LoxType::Class;
          break;
        default:
          break;
        }
        if (instruction.result) {
          valueTypes[instruction.result->id] = instructionType;
        }
        if (instruction.kind == InstructionKind::StoreGlobal &&
            instruction.operands.size() == 1 && !instruction.symbol.empty()) {
          mergeGlobalType(globalTypes, instruction.symbol,
                          valueType(instruction.operands[0], valueTypes));
        }
      }
    }
  }
  return globalTypes;
}

struct StableFunctionTarget {
  std::string functionName;
  int arity = 0;
};

std::unordered_map<std::string, StableFunctionTarget>
collectStableTopLevelFunctions(const LoxModule &module) {
  if (module.functions().empty()) {
    return {};
  }

  const LoxFunction &main = module.functions().front();
  std::unordered_map<uint32_t, std::string> definedFunctionByValue;
  std::unordered_map<std::string, std::string> globalToFunction;
  std::unordered_map<std::string, uint32_t> declarationCounts;
  std::unordered_set<std::string> assignedGlobals;

  for (const auto &block : main.blocks()) {
    for (const auto &instruction : block.instructions()) {
      if (instruction.kind == InstructionKind::DefineFunction &&
          instruction.result && !instruction.symbol.empty()) {
        definedFunctionByValue[instruction.result->id] = instruction.symbol;
        continue;
      }
      if (instruction.kind != InstructionKind::StoreGlobal ||
          instruction.symbol.empty()) {
        continue;
      }
      if (!instruction.declaresSymbol) {
        assignedGlobals.insert(instruction.symbol);
        continue;
      }
      ++declarationCounts[instruction.symbol];
      if (instruction.operands.size() == 1) {
        auto defIt = definedFunctionByValue.find(instruction.operands[0].id);
        if (defIt != definedFunctionByValue.end()) {
          globalToFunction[instruction.symbol] = defIt->second;
        }
      }
    }
  }

  for (const auto &function : module.functions()) {
    for (const auto &block : function.blocks()) {
      for (const auto &instruction : block.instructions()) {
        if (instruction.kind == InstructionKind::StoreGlobal &&
            !instruction.declaresSymbol && !instruction.symbol.empty()) {
          assignedGlobals.insert(instruction.symbol);
        }
      }
    }
  }

  std::unordered_map<std::string, StableFunctionTarget> stableFunctions;
  for (const auto &[globalName, functionName] : globalToFunction) {
    if (assignedGlobals.find(globalName) != assignedGlobals.end()) {
      continue;
    }
    if (declarationCounts[globalName] != 1) {
      continue;
    }
    const LoxFunction *target = module.findFunction(functionName);
    if (!target || !target->upvalues().empty()) {
      continue;
    }
    stableFunctions.emplace(globalName,
                            StableFunctionTarget{functionName,
                                                 target->arity()});
  }
  return stableFunctions;
}

std::unordered_set<std::string>
collectEscapingStableFunctions(
    const LoxModule &module,
    const std::unordered_map<std::string, StableFunctionTarget>
        &stableFunctions) {
  std::unordered_set<std::string> escaping;
  if (stableFunctions.empty()) {
    return escaping;
  }

  for (const auto &function : module.functions()) {
    for (const auto &block : function.blocks()) {
      for (const auto &instruction : block.instructions()) {
        if (instruction.kind != InstructionKind::LoadGlobal ||
            instruction.symbol.empty()) {
          continue;
        }
        auto stableIt = stableFunctions.find(instruction.symbol);
        if (stableIt != stableFunctions.end()) {
          escaping.insert(stableIt->second.functionName);
        }
      }
    }
  }
  return escaping;
}

std::unordered_map<std::string, LoxType>
initialFunctionReturnTypes(const LoxModule &module) {
  std::unordered_map<std::string, LoxType> returnTypes;
  for (const auto &function : module.functions()) {
    returnTypes.emplace(function.name(), LoxType::Unknown);
  }
  return returnTypes;
}

LoxType inferResultType(
    const Instruction &instruction,
    const std::unordered_map<uint32_t, LoxType> &valueTypes,
    const std::unordered_map<std::string, LoxType> &globalTypes,
    const std::unordered_map<std::string, LoxType> &localTypes,
    const std::unordered_map<std::string, LoxType> &functionReturnTypes) {
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
  case InstructionKind::LoadLocal: {
    auto it = localTypes.find(instruction.symbol);
    return it == localTypes.end() ? LoxType::Unknown : it->second;
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
  case InstructionKind::DefineFunction:
    return instruction.resultType == LoxType::Closure ? LoxType::Closure
                                                      : LoxType::Function;
  case InstructionKind::DefineClass:
    return LoxType::Class;
  case InstructionKind::Call:
    if (instruction.operands.size() == 1 &&
        valueType(instruction.operands[0], valueTypes) == LoxType::Class) {
      return LoxType::Instance;
    }
    return instruction.resultType;
  case InstructionKind::DirectCall: {
    auto it = functionReturnTypes.find(instruction.symbol);
    return it == functionReturnTypes.end() ? instruction.resultType
                                           : it->second;
  }
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
    std::unordered_map<std::string, LoxType> functionReturnTypes =
        initialFunctionReturnTypes(module);

    bool moduleChanged = false;
    do {
      moduleChanged = false;
      auto moduleGlobalTypes = collectModuleGlobalTypes(module);
      auto stableFunctions = collectStableTopLevelFunctions(module);
      auto escapingFunctions =
          collectEscapingStableFunctions(module, stableFunctions);
      std::unordered_set<std::string> specializableFunctions;
      for (const auto &entry : stableFunctions) {
        const auto &target = entry.second;
        if (escapingFunctions.find(target.functionName) ==
            escapingFunctions.end()) {
          specializableFunctions.insert(target.functionName);
        }
      }

      std::unordered_map<std::string, std::vector<std::optional<LoxType>>>
          parameterCandidates;
      std::unordered_map<std::string, std::vector<bool>> unknownArguments;
      std::unordered_map<std::string, std::optional<LoxType>>
          knownReturnCandidates;
      std::unordered_set<std::string> unknownReturnFunctions;
      for (const auto &function : module.functions()) {
        parameterCandidates[function.name()].resize(
            function.parameters().size());
        unknownArguments[function.name()].resize(function.parameters().size());
      }

      for (auto &function : module.functions()) {
        std::unordered_map<std::string, LoxType> globalTypes =
            moduleGlobalTypes;
        std::unordered_map<std::string, LoxType> localTypes;
        for (const auto &parameter : function.parameters()) {
          localTypes[parameter.name] = parameter.type;
        }
        bool functionChanged = false;
        do {
          functionChanged = false;
          std::unordered_map<uint32_t, LoxType> valueTypes;
          for (const auto &parameter : function.parameters()) {
            valueTypes[parameter.value.id] = parameter.type;
          }

          for (auto &block : function.blocks()) {
            for (auto &instruction : block.instructions()) {
              LoxType inferred = inferResultType(
                  instruction, valueTypes, globalTypes, localTypes,
                  functionReturnTypes);
              functionChanged |= setResultType(instruction, inferred);
              if (instruction.result) {
                valueTypes[instruction.result->id] = instruction.resultType;
              }
              if (instruction.kind == InstructionKind::StoreLocal &&
                  instruction.operands.size() == 1) {
                functionChanged |= mergeGlobalType(
                    localTypes, instruction.symbol,
                    valueType(instruction.operands[0], valueTypes));
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

        std::unordered_map<uint32_t, LoxType> valueTypes;
        for (const auto &parameter : function.parameters()) {
          valueTypes[parameter.value.id] = parameter.type;
        }

        for (const auto &block : function.blocks()) {
          for (const auto &instruction : block.instructions()) {
            if (instruction.result) {
              valueTypes[instruction.result->id] = instruction.resultType;
            }
            if (instruction.kind == InstructionKind::Return &&
                instruction.operands.size() == 1) {
              LoxType returnType =
                  valueType(instruction.operands[0], valueTypes);
              if (returnType == LoxType::Unknown) {
                unknownReturnFunctions.insert(function.name());
              } else {
                mergeCandidateType(knownReturnCandidates[function.name()],
                                   returnType);
              }
            }
            if (instruction.kind != InstructionKind::DirectCall ||
                instruction.arguments.size() == 0 ||
                specializableFunctions.find(instruction.symbol) ==
                    specializableFunctions.end()) {
              continue;
            }
            auto &candidates = parameterCandidates[instruction.symbol];
            auto &unknowns = unknownArguments[instruction.symbol];
            const size_t count =
                std::min(candidates.size(), instruction.arguments.size());
            for (size_t index = 0; index < count; ++index) {
              LoxType argumentType =
                  valueType(instruction.arguments[index], valueTypes);
              if (argumentType == LoxType::Unknown) {
                if (function.name() != instruction.symbol) {
                  unknowns[index] = true;
                }
                continue;
              }
              mergeCandidateType(candidates[index], argumentType);
            }
          }
        }
      }

      for (auto &function : module.functions()) {
        LoxType knownReturn =
            candidateOrUnknown(knownReturnCandidates[function.name()]);
        const bool hasUnknownReturn =
            unknownReturnFunctions.find(function.name()) !=
            unknownReturnFunctions.end();
        auto returnIt = functionReturnTypes.find(function.name());
        const bool canSeedRecursiveReturn =
            returnIt != functionReturnTypes.end() &&
            returnIt->second == LoxType::Unknown &&
            knownReturn != LoxType::Unknown &&
            specializableFunctions.find(function.name()) !=
                specializableFunctions.end();
        LoxType inferredReturn =
            hasUnknownReturn && !canSeedRecursiveReturn ? LoxType::Unknown
                                                        : knownReturn;
        if (returnIt != functionReturnTypes.end() &&
            returnIt->second != inferredReturn) {
          returnIt->second = inferredReturn;
          moduleChanged = true;
        }
        if (specializableFunctions.find(function.name()) ==
            specializableFunctions.end()) {
          continue;
        }
        auto &candidates = parameterCandidates[function.name()];
        auto &unknowns = unknownArguments[function.name()];
        for (size_t index = 0; index < function.parameters().size(); ++index) {
          LoxType inferredType =
              index < unknowns.size() && unknowns[index]
                  ? LoxType::Unknown
                  : candidateOrUnknown(candidates[index]);
          moduleChanged |=
              setParameterType(function.parameters()[index], inferredType);
        }
      }
      changed |= moduleChanged;
    } while (moduleChanged);

    return changed;
  }
};

void collectGlobalReferences(const LoxFunction &function,
                             std::unordered_set<std::string> &symbols) {
  for (const auto &block : function.blocks()) {
    for (const auto &instruction : block.instructions()) {
      if ((instruction.kind == InstructionKind::LoadGlobal ||
           instruction.kind == InstructionKind::StoreGlobal) &&
          !instruction.symbol.empty()) {
        symbols.insert(instruction.symbol);
      }
    }
  }
}

std::string demotedGlobalSymbol(const std::string &name) {
  std::ostringstream out;
  out << "$global$";
  for (unsigned char ch : name) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '_') {
      out << static_cast<char>(ch);
    } else {
      out << '_';
    }
  }
  return out.str();
}

class GlobalDemotionPass final : public LoxPass {
public:
  std::string name() const override { return "global-demotion"; }

  bool run(LoxModule &module) override {
    if (module.name() != "file_module" || module.functions().empty()) {
      return false;
    }

    LoxFunction &main = module.functions().front();
    std::unordered_set<std::string> escapingGlobals;
    bool first = true;
    for (const auto &function : module.functions()) {
      if (first) {
        first = false;
        continue;
      }
      collectGlobalReferences(function, escapingGlobals);
    }

    std::unordered_set<std::string> candidates;
    for (const auto &block : main.blocks()) {
      for (const auto &instruction : block.instructions()) {
        if (instruction.kind == InstructionKind::StoreGlobal &&
            instruction.declaresSymbol && !instruction.symbol.empty() &&
            escapingGlobals.find(instruction.symbol) == escapingGlobals.end()) {
          candidates.insert(instruction.symbol);
        }
      }
    }

    if (candidates.empty()) {
      return false;
    }

    auto definiteGlobals = computeDefiniteGlobals(main);
    std::unordered_set<std::string> unsafe;
    for (const auto &block : main.blocks()) {
      auto declared = definiteGlobals[block.id().id];
      for (const auto &instruction : block.instructions()) {
        const bool globalAccess =
            instruction.kind == InstructionKind::LoadGlobal ||
            instruction.kind == InstructionKind::StoreGlobal;
        if (!globalAccess || instruction.symbol.empty() ||
            candidates.find(instruction.symbol) == candidates.end()) {
          continue;
        }

        if (instruction.kind == InstructionKind::StoreGlobal &&
            instruction.declaresSymbol) {
          declared.insert(instruction.symbol);
          continue;
        }

        if (declared.find(instruction.symbol) == declared.end()) {
          unsafe.insert(instruction.symbol);
        }
      }
    }
    for (const auto &symbol : unsafe) {
      candidates.erase(symbol);
    }
    if (candidates.empty()) {
      return false;
    }

    std::unordered_map<std::string, std::string> localNames;
    for (const auto &symbol : candidates) {
      localNames.emplace(symbol, demotedGlobalSymbol(symbol));
    }

    bool changed = false;
    for (auto &block : main.blocks()) {
      for (auto &instruction : block.instructions()) {
        auto it = localNames.find(instruction.symbol);
        if (it == localNames.end()) {
          continue;
        }
        if (instruction.kind == InstructionKind::LoadGlobal) {
          instruction.kind = InstructionKind::LoadLocal;
          instruction.symbol = it->second;
          changed = true;
        } else if (instruction.kind == InstructionKind::StoreGlobal) {
          instruction.kind = InstructionKind::StoreLocal;
          instruction.symbol = it->second;
          changed = true;
        }
      }
    }
    return changed;
  }
};

class DirectCallPass final : public LoxPass {
public:
  std::string name() const override { return "direct-call-specialization"; }

  bool run(LoxModule &module) override {
    auto stableFunctions = collectStableTopLevelFunctions(module);
    if (stableFunctions.empty()) {
      return false;
    }

    bool changed = false;
    for (auto &function : module.functions()) {
      std::unordered_map<uint32_t, Instruction *> definitions;
      std::unordered_map<uint32_t, uint32_t> useCounts;
      for (auto &block : function.blocks()) {
        for (auto &instruction : block.instructions()) {
          if (instruction.result) {
            definitions[instruction.result->id] = &instruction;
          }
          for (ValueId operand : instruction.operands) {
            if (operand.valid()) {
              ++useCounts[operand.id];
            }
          }
          for (ValueId argument : instruction.arguments) {
            if (argument.valid()) {
              ++useCounts[argument.id];
            }
          }
        }
      }

      for (auto &block : function.blocks()) {
        for (auto &instruction : block.instructions()) {
          if (instruction.kind != InstructionKind::Call ||
              instruction.operands.size() != 1) {
            continue;
          }
          auto defIt = definitions.find(instruction.operands[0].id);
          if (defIt == definitions.end()) {
            continue;
          }
          Instruction *calleeLoad = defIt->second;
          if (calleeLoad->kind != InstructionKind::LoadGlobal) {
            continue;
          }
          auto targetIt = stableFunctions.find(calleeLoad->symbol);
          if (targetIt == stableFunctions.end()) {
            continue;
          }
          if (static_cast<int>(instruction.arguments.size()) !=
              targetIt->second.arity) {
            continue;
          }

          if (useCounts[instruction.operands[0].id] == 1) {
            replaceWithNil(*calleeLoad);
          }
          instruction.kind = InstructionKind::DirectCall;
          instruction.symbol = targetIt->second.functionName;
          instruction.operands.clear();
          changed = true;
        }
      }
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
        auto definiteGlobals = computeDefiniteGlobals(function);
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
          std::unordered_set<std::string> declaredGlobals =
              definiteGlobals[block.id().id];
          auto &instructions = block.instructions();
          std::vector<Instruction> kept;
          kept.reserve(instructions.size());
          for (auto &instruction : instructions) {
            const bool remove =
                instruction.result && isPure(instruction, declaredGlobals) &&
                usedValues.find(instruction.result->id) == usedValues.end();
            if (!remove) {
              kept.push_back(std::move(instruction));
            }
            if (instruction.kind == InstructionKind::StoreGlobal &&
                instruction.declaresSymbol && !instruction.symbol.empty()) {
              declaredGlobals.insert(instruction.symbol);
            }
          }
          functionChanged |= kept.size() != instructions.size();
          instructions = std::move(kept);
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
  manager.add(std::make_unique<GlobalDemotionPass>());
  manager.add(createTypePropagationPass());
  manager.add(std::make_unique<DirectCallPass>());
  manager.add(createConstantFoldingPass());
  manager.add(createTypePropagationPass());
  manager.add(createDeadCodeEliminationPass());
  return manager;
}

} // namespace eloxir::loxir
