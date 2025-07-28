#include "CodeGenVisitor.h"
#include "../runtime/Value.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>

namespace eloxir {

// Constants mirrored from Value.h
static constexpr uint64_t QNAN = 0x7ff8000000000000ULL;
static constexpr uint64_t MASK_TAG = 0x7ULL << 48;

CodeGenVisitor::CodeGenVisitor(llvm::Module &m)
    : builder(m.getContext()), ctx(m.getContext()), mod(m), value(nullptr) {
  // Declare external runtime fns
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

// --------- Helpers -------------------------------------------------------
llvm::Value *CodeGenVisitor::tagOf(llvm::Value *v) {
  auto mask = llvm::ConstantInt::get(llvmValueTy(), MASK_TAG);
  return builder.CreateAnd(v, mask, "tag");
}

llvm::Value *CodeGenVisitor::isNumber(llvm::Value *v) {
  // Match the runtime logic:
  // 1. If it's not a special NaN pattern, it's a regular number
  // 2. If it matches our QNAN pattern, check if tag bits == 0
  // 3. Other NaN patterns are treated as numbers

  auto specialNaNMask =
      llvm::ConstantInt::get(llvmValueTy(), 0xfff0000000000000ULL);
  auto specialNaNPattern =
      llvm::ConstantInt::get(llvmValueTy(), 0x7ff0000000000000ULL);
  auto qnanMask = llvm::ConstantInt::get(llvmValueTy(), 0xfff8000000000000ULL);
  auto qnanPattern =
      llvm::ConstantInt::get(llvmValueTy(), 0x7ff8000000000000ULL);
  auto zero = llvm::ConstantInt::get(llvmValueTy(), 0);

  // Check if it's not a special NaN pattern at all
  auto maskedBits = builder.CreateAnd(v, specialNaNMask, "masked");
  auto isNotSpecialNaN =
      builder.CreateICmpNE(maskedBits, specialNaNPattern, "notspecialnan");

  // Check if it matches our QNAN pattern
  auto qnanMasked = builder.CreateAnd(v, qnanMask, "qnanmasked");
  auto isOurQNaN = builder.CreateICmpEQ(qnanMasked, qnanPattern, "isourqnan");

  // If it's our QNAN, check if tag bits are 0
  auto tagBits = tagOf(v);
  auto hasZeroTag = builder.CreateICmpEQ(tagBits, zero, "zerotag");
  auto isTaggedNumber =
      builder.CreateAnd(isOurQNaN, hasZeroTag, "taggednumber");

  // It's a number if: not special NaN OR (our QNAN with zero tag)
  return builder.CreateOr(isNotSpecialNaN, isTaggedNumber, "isnum");
}

llvm::Value *CodeGenVisitor::toDouble(llvm::Value *v) {
  // bitcast i64 -> double
  return builder.CreateBitCast(v, llvm::Type::getDoubleTy(ctx), "asdouble");
}

llvm::Value *CodeGenVisitor::fromDouble(llvm::Value *d) {
  // bitcast double -> i64
  return builder.CreateBitCast(d, llvmValueTy(), "fromdouble");
}

llvm::Value *CodeGenVisitor::isFalsy(llvm::Value *v) {
  auto tag = tagOf(v);
  auto boolTag = llvm::ConstantInt::get(
      llvmValueTy(), (static_cast<uint64_t>(Tag::BOOL) << 48));
  auto nilTag = llvm::ConstantInt::get(llvmValueTy(),
                                       (static_cast<uint64_t>(Tag::NIL) << 48));

  auto isBool = builder.CreateICmpEQ(tag, boolTag, "isBool");
  auto isNil = builder.CreateICmpEQ(tag, nilTag, "isNil");

  auto lowBit64 = builder.CreateAnd(v, llvm::ConstantInt::get(llvmValueTy(), 1),
                                    "lowbit64");
  auto lowBit = builder.CreateTrunc(lowBit64, builder.getInt1Ty(), "lowbit");

  auto isFalseBool = builder.CreateAnd(
      isBool, builder.CreateICmpEQ(lowBit, builder.getFalse()));
  return builder.CreateOr(isFalseBool, isNil, "isFalsy");
}

llvm::Value *CodeGenVisitor::isTruthy(llvm::Value *v) {
  return builder.CreateNot(isFalsy(v), "isTruthy");
}

llvm::Value *CodeGenVisitor::boolConst(bool b) {
  uint64_t bits =
      QNAN | (static_cast<uint64_t>(Tag::BOOL) << 48) | (b ? 1ULL : 0ULL);
  return llvm::ConstantInt::get(llvmValueTy(), bits);
}

llvm::Value *CodeGenVisitor::nilConst() {
  uint64_t bits = QNAN | (static_cast<uint64_t>(Tag::NIL) << 48);
  return llvm::ConstantInt::get(llvmValueTy(), bits);
}

llvm::Value *CodeGenVisitor::makeBool(llvm::Value *i1) {
  // zero-extend to i64 then OR into a BOOL tagged value
  auto zext = builder.CreateZExt(i1, llvmValueTy(), "bext");
  auto base = llvm::ConstantInt::get(
      llvmValueTy(), QNAN | (static_cast<uint64_t>(Tag::BOOL) << 48));
  return builder.CreateOr(base, zext, "boolval");
}

// Helper function for proper equality comparison following Lox semantics
llvm::Value *CodeGenVisitor::valuesEqual(llvm::Value *L, llvm::Value *R) {
  auto tagL = tagOf(L);
  auto tagR = tagOf(R);

  // Different types are never equal
  auto sameType = builder.CreateICmpEQ(tagL, tagR, "sametype");

  auto fn = builder.GetInsertBlock()->getParent();
  auto sameTypeBB = llvm::BasicBlock::Create(ctx, "sametype", fn);
  auto diffTypeBB = llvm::BasicBlock::Create(ctx, "difftype", fn);
  auto contBB = llvm::BasicBlock::Create(ctx, "eq.cont", fn);

  builder.CreateCondBr(sameType, sameTypeBB, diffTypeBB);

  // Different types - return false
  builder.SetInsertPoint(diffTypeBB);
  auto falseVal = builder.getFalse();
  builder.CreateBr(contBB);

  // Same types - compare based on type
  builder.SetInsertPoint(sameTypeBB);
  auto zero = llvm::ConstantInt::get(llvmValueTy(), 0);
  auto numTag = llvm::ConstantInt::get(
      llvmValueTy(), static_cast<uint64_t>(Tag::NUMBER) << 48);
  auto boolTag = llvm::ConstantInt::get(llvmValueTy(),
                                        static_cast<uint64_t>(Tag::BOOL) << 48);

  auto isNumBB = llvm::BasicBlock::Create(ctx, "eq.num", fn);
  auto isBoolOrNilBB = llvm::BasicBlock::Create(ctx, "eq.boolnil", fn);

  auto isNum = builder.CreateICmpEQ(tagL, numTag, "isnum");
  builder.CreateCondBr(isNum, isNumBB, isBoolOrNilBB);

  // Numbers: use floating-point comparison (handles NaN correctly)
  builder.SetInsertPoint(isNumBB);
  auto Ld = toDouble(L);
  auto Rd = toDouble(R);
  auto numEqual = builder.CreateFCmpOEQ(Ld, Rd, "numeq");
  builder.CreateBr(contBB);

  // Booleans and nil: bitwise comparison works (they have same tag, so just
  // compare bits)
  builder.SetInsertPoint(isBoolOrNilBB);
  auto bitsEqual = builder.CreateICmpEQ(L, R, "bitseq");
  builder.CreateBr(contBB);

  // Merge results
  builder.SetInsertPoint(contBB);
  auto phi = builder.CreatePHI(builder.getInt1Ty(), 3, "eq.res");
  phi->addIncoming(falseVal, diffTypeBB);
  phi->addIncoming(numEqual, isNumBB);
  phi->addIncoming(bitsEqual, isBoolOrNilBB);

  return phi;
}

// Helper to check if both values are numbers and generate runtime error if not
llvm::Value *CodeGenVisitor::checkBothNumbers(llvm::Value *L, llvm::Value *R,
                                              llvm::BasicBlock *&successBB,
                                              llvm::BasicBlock *&errorBB) {
  auto fn = builder.GetInsertBlock()->getParent();
  successBB = llvm::BasicBlock::Create(ctx, "both_numbers", fn);
  errorBB = llvm::BasicBlock::Create(ctx, "type_error", fn);

  auto isLNum = isNumber(L);
  auto isRNum = isNumber(R);
  auto both = builder.CreateAnd(isLNum, isRNum, "bothnum");

  builder.CreateCondBr(both, successBB, errorBB);

  // Error case - generate runtime error (trap for now)
  builder.SetInsertPoint(errorBB);
  auto trap = llvm::Intrinsic::getDeclaration(&mod, llvm::Intrinsic::trap);
  builder.CreateCall(trap);
  builder.CreateUnreachable();

  return both;
}

// --------- Expr visitors -------------------------------------------------
void CodeGenVisitor::visitBinaryExpr(Binary *e) {
  e->left->accept(this);
  llvm::Value *L = value;
  e->right->accept(this);
  llvm::Value *R = value;

  auto fn = builder.GetInsertBlock()->getParent();

  // Handle equality operators separately (they work on any types)
  if (e->op.getType() == TokenType::EQUAL_EQUAL) {
    auto equal = valuesEqual(L, R);
    value = makeBool(equal);
    return;
  }

  if (e->op.getType() == TokenType::BANG_EQUAL) {
    auto equal = valuesEqual(L, R);
    auto notEqual = builder.CreateNot(equal, "ne");
    value = makeBool(notEqual);
    return;
  }

  // For arithmetic and ordering operators, both operands must be numbers
  llvm::BasicBlock *bothNumBB;
  llvm::BasicBlock *errorBB;
  checkBothNumbers(L, R, bothNumBB, errorBB);

  // Fast path: both are numbers
  builder.SetInsertPoint(bothNumBB);
  llvm::Value *Ld = toDouble(L);
  llvm::Value *Rd = toDouble(R);
  llvm::Value *res = nullptr;

  switch (e->op.getType()) {
  case TokenType::PLUS:
    res = fromDouble(builder.CreateFAdd(Ld, Rd, "add"));
    break;
  case TokenType::MINUS:
    res = fromDouble(builder.CreateFSub(Ld, Rd, "sub"));
    break;
  case TokenType::STAR:
    res = fromDouble(builder.CreateFMul(Ld, Rd, "mul"));
    break;
  case TokenType::SLASH:
    res = fromDouble(builder.CreateFDiv(Ld, Rd, "div"));
    break;

  // Use ordered comparisons to handle NaN properly
  // In Lox, all comparisons with NaN should return false
  case TokenType::GREATER:
    res = makeBool(builder.CreateFCmpOGT(Ld, Rd, "gt"));
    break;
  case TokenType::GREATER_EQUAL:
    res = makeBool(builder.CreateFCmpOGE(Ld, Rd, "ge"));
    break;
  case TokenType::LESS:
    res = makeBool(builder.CreateFCmpOLT(Ld, Rd, "lt"));
    break;
  case TokenType::LESS_EQUAL:
    res = makeBool(builder.CreateFCmpOLE(Ld, Rd, "le"));
    break;

  default:
    res = nilConst(); // shouldn't happen
    break;
  }

  value = res;
}

void CodeGenVisitor::visitGroupingExpr(Grouping *e) {
  e->expression->accept(this);
}

void CodeGenVisitor::visitLiteralExpr(Literal *e) {
  if (std::holds_alternative<double>(e->value)) {
    double d = std::get<double>(e->value);
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    value = llvm::ConstantInt::get(llvmValueTy(), bits);
  } else if (std::holds_alternative<std::string>(e->value)) {
    // Not implemented yet: return nil for now
    value = nilConst();
  } else if (std::holds_alternative<bool>(e->value)) {
    value = boolConst(std::get<bool>(e->value));
  } else {
    value = nilConst();
  }
}

void CodeGenVisitor::visitUnaryExpr(Unary *e) {
  e->right->accept(this);
  llvm::Value *R = value;

  switch (e->op.getType()) {
  case TokenType::MINUS: {
    auto fn = builder.GetInsertBlock()->getParent();
    auto isNumBB = llvm::BasicBlock::Create(ctx, "neg.num", fn);
    auto slowBB = llvm::BasicBlock::Create(ctx, "neg.slow", fn);
    auto contBB = llvm::BasicBlock::Create(ctx, "neg.cont", fn);

    builder.CreateCondBr(isNumber(R), isNumBB, slowBB);

    builder.SetInsertPoint(isNumBB);
    auto d = toDouble(R);
    auto nd = builder.CreateFNeg(d, "neg");
    auto rv = fromDouble(nd);
    builder.CreateBr(contBB);

    builder.SetInsertPoint(slowBB);
    auto trap = llvm::Intrinsic::getDeclaration(&mod, llvm::Intrinsic::trap);
    builder.CreateCall(trap);
    builder.CreateUnreachable();

    builder.SetInsertPoint(contBB);
    auto phi = builder.CreatePHI(llvmValueTy(), 1, "neg.res");
    phi->addIncoming(rv, isNumBB);
    value = phi;
    break;
  }
  case TokenType::BANG: {
    auto falsy = isFalsy(R);
    value = makeBool(falsy); // !x is true when x is falsy
    break;
  }
  default:
    value = R; // shouldn't happen
    break;
  }
}

void CodeGenVisitor::visitVariableExpr(Variable *e) {
  auto it = locals.find(e->name.getLexeme());
  if (it == locals.end()) {
    value = nilConst();
    return;
  }
  value = builder.CreateLoad(llvmValueTy(), it->second,
                             e->name.getLexeme().c_str());
}

void CodeGenVisitor::visitAssignExpr(Assign *e) {
  e->value->accept(this);
  auto it = locals.find(e->name.getLexeme());
  if (it == locals.end()) {
    // create one lazily in current function entry
    auto fn = builder.GetInsertBlock()->getParent();
    auto &entry = fn->getEntryBlock();
    llvm::IRBuilder<> save(entry.getFirstNonPHI());
    auto slot =
        save.CreateAlloca(llvmValueTy(), nullptr, e->name.getLexeme().c_str());
    locals[e->name.getLexeme()] = slot;
    it = locals.find(e->name.getLexeme());
  }
  builder.CreateStore(value, it->second);
}

void CodeGenVisitor::visitLogicalExpr(Logical *e) {
  // Evaluate left
  e->left->accept(this);
  llvm::Value *L = value;

  auto fn = builder.GetInsertBlock()->getParent();
  auto leftBB = builder.GetInsertBlock();
  auto rightBB = llvm::BasicBlock::Create(ctx, "logic.right", fn);
  auto endBB = llvm::BasicBlock::Create(ctx, "logic.end", fn);

  llvm::Value *leftTruthy = isTruthy(L);

  if (e->op.getType() == TokenType::OR) {
    // if left is truthy -> skip right
    builder.CreateCondBr(leftTruthy, endBB, rightBB);
  } else { // AND
    // if left is truthy -> evaluate right, else skip
    builder.CreateCondBr(leftTruthy, rightBB, endBB);
  }

  // Right side
  builder.SetInsertPoint(rightBB);
  e->right->accept(this);
  llvm::Value *R = value;
  builder.CreateBr(endBB);
  auto rightEvalBB = builder.GetInsertBlock();

  // Merge
  builder.SetInsertPoint(endBB);
  auto phi = builder.CreatePHI(llvmValueTy(), 2, "logic.res");
  if (e->op.getType() == TokenType::OR) {
    phi->addIncoming(L, leftBB);
    phi->addIncoming(R, rightEvalBB);
  } else { // AND
    phi->addIncoming(R, rightEvalBB);
    phi->addIncoming(L, leftBB);
  }
  value = phi;
}

void CodeGenVisitor::visitCallExpr(Call *e) {
  // Not on the critical path -> return nil for now
  value = nilConst();
}

// --------- Stmt visitors -------------------------------------------------
void CodeGenVisitor::visitExpressionStmt(Expression *s) {
  s->expression->accept(this);
}

void CodeGenVisitor::visitPrintStmt(Print *s) {
  s->expression->accept(this);
  llvm::Function *printFn = mod.getFunction("elx_print");
  builder.CreateCall(printFn, {value});
}

void CodeGenVisitor::visitVarStmt(Var *s) {
  if (s->initializer)
    s->initializer->accept(this);
  else
    value = nilConst();

  auto fn = builder.GetInsertBlock()->getParent();
  auto &entry = fn->getEntryBlock();
  llvm::IRBuilder<> save(entry.getFirstNonPHI());
  auto slot =
      save.CreateAlloca(llvmValueTy(), nullptr, s->name.getLexeme().c_str());
  builder.CreateStore(value, slot);
  locals[s->name.getLexeme()] = slot;
}

void CodeGenVisitor::visitBlockStmt(Block *s) {
  // Simple block: push/pop scope in locals map
  auto before = locals;
  for (auto &st : s->statements)
    st->accept(this);
  locals = std::move(before);
}

void CodeGenVisitor::visitIfStmt(If *s) {
  s->condition->accept(this);
  llvm::Value *cond = value;

  auto fn = builder.GetInsertBlock()->getParent();
  auto thenBB = llvm::BasicBlock::Create(ctx, "if.then", fn);
  auto elseBB = llvm::BasicBlock::Create(ctx, "if.else");
  auto mergeBB = llvm::BasicBlock::Create(ctx, "if.end");

  // Treat non-false/nil as true (reuse same truthiness as in !)
  auto tag = tagOf(cond);
  auto boolTag = llvm::ConstantInt::get(
      llvmValueTy(), (static_cast<uint64_t>(Tag::BOOL) << 48));
  auto nilTag = llvm::ConstantInt::get(llvmValueTy(),
                                       (static_cast<uint64_t>(Tag::NIL) << 48));
  auto isBool = builder.CreateICmpEQ(tag, boolTag);
  auto isNil = builder.CreateICmpEQ(tag, nilTag);
  auto lowBit = builder.CreateTrunc(
      builder.CreateAnd(cond, llvm::ConstantInt::get(llvmValueTy(), 1)),
      builder.getInt1Ty());
  auto isFalseBool = builder.CreateAnd(
      isBool, builder.CreateICmpEQ(lowBit, builder.getFalse()));
  auto isFalse = builder.CreateOr(isFalseBool, isNil);
  auto condI1 = builder.CreateNot(isFalse, "ifcond");

  builder.CreateCondBr(condI1, thenBB, elseBB);

  builder.SetInsertPoint(thenBB);
  s->thenBranch->accept(this);
  builder.CreateBr(mergeBB);

  builder.SetInsertPoint(elseBB);
  if (s->elseBranch)
    s->elseBranch->accept(this);
  builder.CreateBr(mergeBB);

  builder.SetInsertPoint(mergeBB);
  // No result for statements.
  value = nilConst();
}

void CodeGenVisitor::visitWhileStmt(While *s) {
  auto fn = builder.GetInsertBlock()->getParent();
  auto condBB = llvm::BasicBlock::Create(ctx, "while.cond", fn);
  auto bodyBB = llvm::BasicBlock::Create(ctx, "while.body");
  auto endBB = llvm::BasicBlock::Create(ctx, "while.end");

  builder.CreateBr(condBB);

  builder.SetInsertPoint(condBB);
  s->condition->accept(this);
  llvm::Value *cond = value;
  // truthiness as before
  auto tag = tagOf(cond);
  auto boolTag = llvm::ConstantInt::get(
      llvmValueTy(), (static_cast<uint64_t>(Tag::BOOL) << 48));
  auto nilTag = llvm::ConstantInt::get(llvmValueTy(),
                                       (static_cast<uint64_t>(Tag::NIL) << 48));
  auto isBool = builder.CreateICmpEQ(tag, boolTag);
  auto isNil = builder.CreateICmpEQ(tag, nilTag);
  auto lowBit = builder.CreateTrunc(
      builder.CreateAnd(cond, llvm::ConstantInt::get(llvmValueTy(), 1)),
      builder.getInt1Ty());
  auto isFalseBool = builder.CreateAnd(
      isBool, builder.CreateICmpEQ(lowBit, builder.getFalse()));
  auto isFalse = builder.CreateOr(isFalseBool, isNil);
  auto condI1 = builder.CreateNot(isFalse, "whilecond");
  builder.CreateCondBr(condI1, bodyBB, endBB);

  builder.SetInsertPoint(bodyBB);
  s->body->accept(this);
  builder.CreateBr(condBB);

  builder.SetInsertPoint(endBB);
  value = nilConst();
}

void CodeGenVisitor::visitFunctionStmt(Function *s) { value = nilConst(); }
void CodeGenVisitor::visitReturnStmt(Return *s) { value = nilConst(); }
void CodeGenVisitor::visitGetExpr(Get *e) { value = nilConst(); }
void CodeGenVisitor::visitSetExpr(Set *e) { value = nilConst(); }
void CodeGenVisitor::visitThisExpr(This *e) { value = nilConst(); }
void CodeGenVisitor::visitSuperExpr(Super *e) { value = nilConst(); }
void CodeGenVisitor::visitClassStmt(Class *s) {}

} // namespace eloxir
