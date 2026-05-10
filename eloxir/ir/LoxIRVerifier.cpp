#include "LoxIRVerifier.h"

#include <sstream>
#include <unordered_set>

namespace eloxir::loxir {

namespace {

struct ValueIdHash {
  size_t operator()(ValueId value) const { return value.id; }
};

std::string valueName(ValueId value) {
  std::ostringstream out;
  out << '%' << value.id;
  return out.str();
}

std::string blockName(BlockId block) {
  std::ostringstream out;
  out << "^bb" << block.id;
  return out.str();
}

void addError(VerificationResult &result, const std::string &message) {
  result.ok = false;
  result.errors.push_back(message);
}

bool blockExists(const LoxFunction &function, BlockId id) {
  return id.valid() && function.findBlock(id) != nullptr;
}

void verifyInstruction(const LoxFunction &function, const BasicBlock &block,
                       const Instruction &instruction,
                       std::unordered_set<ValueId, ValueIdHash> &definedValues,
                       VerificationResult &result) {
  if (instruction.result) {
    if (definedValues.count(*instruction.result) > 0) {
      addError(result, "duplicate value " + valueName(*instruction.result) +
                           " in function @" + function.name());
    }
    definedValues.insert(*instruction.result);
  }

  auto verifyOperand = [&](ValueId value) {
    if (!value.valid() || definedValues.count(value) == 0) {
      addError(result, "undefined operand " + valueName(value) +
                           " in block " + blockName(block.id()) +
                           " of function @" + function.name());
    }
  };

  for (ValueId operand : instruction.operands) {
    verifyOperand(operand);
  }
  for (ValueId argument : instruction.arguments) {
    verifyOperand(argument);
  }

  if ((instruction.kind == InstructionKind::Jump ||
       instruction.kind == InstructionKind::Branch) &&
      !blockExists(function, instruction.target)) {
    addError(result, "invalid branch target " + blockName(instruction.target) +
                         " in function @" + function.name());
  }
  if (instruction.kind == InstructionKind::Branch &&
      !blockExists(function, instruction.falseTarget)) {
    addError(result, "invalid false branch target " +
                         blockName(instruction.falseTarget) +
                         " in function @" + function.name());
  }
}

void verifyBlock(const LoxFunction &function, const BasicBlock &block,
                 std::unordered_set<ValueId, ValueIdHash> &definedValues,
                 VerificationResult &result) {
  bool sawTerminator = false;
  for (const Instruction &instruction : block.instructions()) {
    if (sawTerminator) {
      addError(result, "instruction after terminator in block " +
                           blockName(block.id()) + " of function @" +
                           function.name());
      break;
    }

    verifyInstruction(function, block, instruction, definedValues, result);
    sawTerminator = isTerminator(instruction.kind);
  }

  if (!block.hasTerminator()) {
    addError(result, "block " + blockName(block.id()) + " of function @" +
                         function.name() + " has no terminator");
  }
}

void verifyFunction(const LoxFunction &function, VerificationResult &result) {
  if (function.blocks().empty()) {
    addError(result, "function @" + function.name() + " has no blocks");
    return;
  }

  std::unordered_set<ValueId, ValueIdHash> definedValues;
  for (const Parameter &parameter : function.parameters()) {
    if (!parameter.value.valid()) {
      addError(result, "parameter " + parameter.name + " in function @" +
                           function.name() + " has invalid value id");
    }
    definedValues.insert(parameter.value);
  }

  for (const BasicBlock &block : function.blocks()) {
    verifyBlock(function, block, definedValues, result);
  }
}

} // namespace

VerificationResult verifyModule(const LoxModule &module) {
  VerificationResult result;
  if (module.functions().empty()) {
    addError(result, "module @" + module.name() + " has no functions");
    return result;
  }

  for (const LoxFunction &function : module.functions()) {
    verifyFunction(function, result);
  }
  return result;
}

} // namespace eloxir::loxir
