#include "CodeGenVisitor.h"
#include "../runtime/Value.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>

namespace eloxir {

CodeGenVisitor::CodeGenVisitor(llvm::Module &m)
    : builder(m.getContext()), ctx(m.getContext()), mod(m), value(nullptr) {
  // Prepare the external functions we need
  llvm::FunctionType *printFnTy =
      llvm::FunctionType::get(llvmValueTy(), {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_print", printFnTy);

  llvm::FunctionType *clockFnTy =
      llvm::FunctionType::get(llvmValueTy(), {}, false);
  mod.getOrInsertFunction("elx_clock", clockFnTy);
}

llvm::Type *CodeGenVisitor::llvmValueTy() const {
  return llvm::Type::getInt64Ty(ctx);
}

llvm::Value *CodeGenVisitor::tagOf(llvm::Value *v) {
  uint64_t maskTag = static_cast<uint64_t>(0x7ULL) << 48;
  llvm::Value *mask =
      llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx), maskTag);
  return builder.CreateAnd(v, mask);
}

void CodeGenVisitor::visitBinaryExpr(Binary *e) {
  e->left->accept(this);
  llvm::Value *l = value;

  e->right->accept(this);
  llvm::Value *r = value;

  // For simple demo, we'll just implement basic numeric addition
  // In a real implementation, this would include tag checks and more operations
  if (e->op.getType() == TokenType::PLUS) {
    // Create a basic block for numeric fast-path
    auto fn = builder.GetInsertBlock()->getParent();
    auto numBB = llvm::BasicBlock::Create(ctx, "num", fn);
    auto doneBB = llvm::BasicBlock::Create(ctx, "done", fn);

    // Check if both values are numbers
    auto lTag = tagOf(l);
    auto rTag = tagOf(r);
    auto isNum = builder.CreateICmpEQ(lTag, rTag);
    builder.CreateCondBr(isNum, numBB, doneBB);

    // Numeric fast-path
    builder.SetInsertPoint(numBB);

    // Extract the double values (this is simplified)
    auto lDouble = builder.CreateBitCast(l, llvm::Type::getDoubleTy(ctx));
    auto rDouble = builder.CreateBitCast(r, llvm::Type::getDoubleTy(ctx));

    // Add them
    auto result = builder.CreateFAdd(lDouble, rDouble);

    // Convert back to Value
    auto numResult = builder.CreateBitCast(result, llvmValueTy());
    builder.CreateBr(doneBB);

    // Merge results
    builder.SetInsertPoint(doneBB);
    auto phi = builder.CreatePHI(llvmValueTy(), 2);
    phi->addIncoming(numResult, numBB);
    phi->addIncoming(l,
                     &fn->getEntryBlock()); // Default to left operand for error

    value = phi;
  } else {
    // For demo, just return left operand for other operations
    value = l;
  }
}

void CodeGenVisitor::visitGroupingExpr(Grouping *e) {
  e->expression->accept(this);
  // value is already set by the nested expression
}

void CodeGenVisitor::visitLiteralExpr(Literal *e) {
  if (std::holds_alternative<double>(e->value)) {
    // Create a Value::number
    double d = std::get<double>(e->value);
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    value = llvm::ConstantInt::get(llvmValueTy(), bits);
  } else if (std::holds_alternative<std::string>(e->value)) {
    // For demo: Just create a simple string representation (not real)
    // In real implementation, this would create a string object
    const std::string &s = std::get<std::string>(e->value);

    // For now, return a nil value
    uint64_t nilValue =
        0x7ffc000000000000ULL | (static_cast<uint64_t>(Tag::NIL) << 48);
    value = llvm::ConstantInt::get(llvmValueTy(), nilValue);
  } else if (std::holds_alternative<bool>(e->value)) {
    bool b = std::get<bool>(e->value);
    uint64_t boolValue = 0x7ffc000000000000ULL |
                         (static_cast<uint64_t>(Tag::BOOL) << 48) | (b ? 1 : 0);
    value = llvm::ConstantInt::get(llvmValueTy(), boolValue);
  } else if (std::holds_alternative<std::monostate>(e->value)) {
    // nil (monostate represents nil in our variant)
    uint64_t nilValue =
        0x7ffc000000000000ULL | (static_cast<uint64_t>(Tag::NIL) << 48);
    value = llvm::ConstantInt::get(llvmValueTy(), nilValue);
  } else {
    // Default fallback to nil
    uint64_t nilValue =
        0x7ffc000000000000ULL | (static_cast<uint64_t>(Tag::NIL) << 48);
    value = llvm::ConstantInt::get(llvmValueTy(), nilValue);
  }
}

void CodeGenVisitor::visitUnaryExpr(Unary *e) {
  e->right->accept(this);
  // Simplified for demo
  // For a real implementation, this would handle operators like - and !
}

void CodeGenVisitor::visitVariableExpr(Variable *e) {
  // For demo, just return nil
  // In real implementation, this would look up the variable
  uint64_t nilValue =
      0x7ffc000000000000ULL | (static_cast<uint64_t>(Tag::NIL) << 48);
  value = llvm::ConstantInt::get(llvmValueTy(), nilValue);
}

void CodeGenVisitor::visitAssignExpr(Assign *e) {
  // Simplified for demo
  e->value->accept(this);
  // In real implementation, this would store to the variable
}

void CodeGenVisitor::visitLogicalExpr(Logical *e) {
  // Simplified for demo
  e->left->accept(this);
  // In real implementation, this would implement short-circuit evaluation
}

void CodeGenVisitor::visitCallExpr(Call *e) {
  // Simplified for demo
  // In real implementation, this would call the function
  uint64_t nilValue =
      0x7ffc000000000000ULL | (static_cast<uint64_t>(Tag::NIL) << 48);
  value = llvm::ConstantInt::get(llvmValueTy(), nilValue);
}

void CodeGenVisitor::visitExpressionStmt(Expression *s) {
  s->expression->accept(this);
  // Drop result (REPL wraps in print)
}

void CodeGenVisitor::visitPrintStmt(Print *s) {
  s->expression->accept(this);

  // Call elx_print with the result
  llvm::Function *printFn = mod.getFunction("elx_print");
  builder.CreateCall(printFn, {value});
}

void CodeGenVisitor::visitVarStmt(Var *s) {
  // Simplified for demo
  if (s->initializer) {
    s->initializer->accept(this);
  } else {
    uint64_t nilValue =
        0x7ffc000000000000ULL | (static_cast<uint64_t>(Tag::NIL) << 48);
    value = llvm::ConstantInt::get(llvmValueTy(), nilValue);
  }

  // In real implementation, this would store to the variable
}

void CodeGenVisitor::visitBlockStmt(Block *s) {
  // Simplified for demo
  for (const auto &statement : s->statements) {
    statement->accept(this);
  }
}

void CodeGenVisitor::visitIfStmt(If *s) {
  // Simplified for demo
  s->condition->accept(this);
  // In real implementation, this would create branch instructions
  s->thenBranch->accept(this);
  if (s->elseBranch) {
    s->elseBranch->accept(this);
  }
}

void CodeGenVisitor::visitWhileStmt(While *s) {
  // Simplified for demo
  // In real implementation, this would create a loop
  s->body->accept(this);
}

void CodeGenVisitor::visitFunctionStmt(Function *s) {
  // Simplified for demo
  // In real implementation, this would create a function
}

void CodeGenVisitor::visitReturnStmt(Return *s) {
  // Simplified for demo
  if (s->value) {
    s->value->accept(this);
  } else {
    uint64_t nilValue =
        0x7ffc000000000000ULL | (static_cast<uint64_t>(Tag::NIL) << 48);
    value = llvm::ConstantInt::get(llvmValueTy(), nilValue);
  }

  // In real implementation, this would create a return instruction
}

void CodeGenVisitor::visitGetExpr(Get *e) {
  // TODO: Implement property access
  (void)e; // Suppress unused parameter warning
  value = nullptr;
}

void CodeGenVisitor::visitSetExpr(Set *e) {
  // TODO: Implement property assignment
  (void)e; // Suppress unused parameter warning
  value = nullptr;
}

void CodeGenVisitor::visitThisExpr(This *e) {
  // TODO: Implement 'this' keyword
  (void)e; // Suppress unused parameter warning
  value = nullptr;
}

void CodeGenVisitor::visitSuperExpr(Super *e) {
  // TODO: Implement 'super' keyword
  (void)e; // Suppress unused parameter warning
  value = nullptr;
}

void CodeGenVisitor::visitClassStmt(Class *s) {
  // TODO: Implement class declaration
  (void)s; // Suppress unused parameter warning
}

} // namespace eloxir
