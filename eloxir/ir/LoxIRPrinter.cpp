#include "LoxIRPrinter.h"

#include <ostream>
#include <sstream>

namespace eloxir::loxir {

namespace {

void printValue(std::ostream &out, ValueId value) { out << '%' << value.id; }

void printBlockRef(std::ostream &out, BlockId block) {
  out << "^bb" << block.id;
}

void printOperands(std::ostream &out, const std::vector<ValueId> &operands) {
  for (ValueId operand : operands) {
    out << ' ';
    printValue(out, operand);
  }
}

void printInstruction(std::ostream &out, const Instruction &instruction) {
  out << "    ";
  if (instruction.result) {
    printValue(out, *instruction.result);
    out << ':' << toString(instruction.resultType) << " = ";
  }

  out << toString(instruction.kind);
  switch (instruction.kind) {
  case InstructionKind::ConstantBool:
    out << ' ' << (instruction.boolValue ? "true" : "false");
    break;
  case InstructionKind::ConstantNumber:
    out << ' ' << instruction.numberValue;
    break;
  case InstructionKind::ConstantString:
  case InstructionKind::LoadLocal:
  case InstructionKind::StoreLocal:
  case InstructionKind::LoadGlobal:
  case InstructionKind::StoreGlobal:
  case InstructionKind::LoadUpvalue:
  case InstructionKind::StoreUpvalue:
  case InstructionKind::GetProperty:
  case InstructionKind::SetProperty:
  case InstructionKind::DefineFunction:
  case InstructionKind::DefineClass:
  case InstructionKind::DefineMethod:
  case InstructionKind::Call:
    if (!instruction.symbol.empty()) {
      out << " @" << instruction.symbol;
    }
    break;
  case InstructionKind::Binary:
    out << ' ' << toString(instruction.binaryOp);
    break;
  case InstructionKind::Unary:
    out << ' ' << toString(instruction.unaryOp);
    break;
  default:
    break;
  }

  printOperands(out, instruction.operands);
  if (!instruction.arguments.empty()) {
    out << " args(";
    for (size_t i = 0; i < instruction.arguments.size(); ++i) {
      if (i != 0) {
        out << ", ";
      }
      printValue(out, instruction.arguments[i]);
    }
    out << ')';
  }

  if (instruction.target.valid()) {
    out << " -> ";
    printBlockRef(out, instruction.target);
  }
  if (instruction.falseTarget.valid()) {
    out << ", ";
    printBlockRef(out, instruction.falseTarget);
  }
  if (instruction.source.line > 0) {
    out << " ; line " << instruction.source.line;
  }
  out << '\n';
}

} // namespace

void printModule(std::ostream &out, const LoxModule &module) {
  out << "module @" << module.name() << " {\n";
  for (const LoxFunction &function : module.functions()) {
    out << "  func @" << function.name() << '(';
    for (size_t i = 0; i < function.parameters().size(); ++i) {
      const Parameter &parameter = function.parameters()[i];
      if (i != 0) {
        out << ", ";
      }
      out << '%' << parameter.value.id << ':' << toString(parameter.type)
          << ' ' << parameter.name;
    }
    out << ") {\n";

    for (const BasicBlock &block : function.blocks()) {
      out << "  ^bb" << block.id().id << " " << block.name() << ":\n";
      for (const Instruction &instruction : block.instructions()) {
        printInstruction(out, instruction);
      }
    }

    out << "  }\n";
  }
  out << "}\n";
}

std::string moduleToString(const LoxModule &module) {
  std::ostringstream out;
  printModule(out, module);
  return out.str();
}

} // namespace eloxir::loxir
