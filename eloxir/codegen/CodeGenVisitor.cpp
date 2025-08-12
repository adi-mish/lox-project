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
    : builder(m.getContext()), ctx(m.getContext()), mod(m), value(nullptr),
      currentFunction(nullptr), builtinsInitialized(false) {
  // Declare external runtime fns
  llvm::FunctionType *printFnTy =
      llvm::FunctionType::get(llvmValueTy(), {llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_print", printFnTy);

  llvm::FunctionType *clockFnTy =
      llvm::FunctionType::get(llvmValueTy(), {}, false);
  mod.getOrInsertFunction("elx_clock", clockFnTy);

  // String functions
  llvm::FunctionType *allocStringTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0),
       llvm::Type::getInt32Ty(ctx)},
      false);
  mod.getOrInsertFunction("elx_allocate_string", allocStringTy);

  llvm::FunctionType *concatTy = llvm::FunctionType::get(
      llvmValueTy(), {llvmValueTy(), llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_concatenate_strings", concatTy);

  llvm::FunctionType *strEqualTy = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(ctx), {llvmValueTy(), llvmValueTy()}, false);
  mod.getOrInsertFunction("elx_strings_equal", strEqualTy);

  // Function functions
  llvm::FunctionType *allocFuncTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0),
       llvm::Type::getInt32Ty(ctx),
       llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0)},
      false);
  mod.getOrInsertFunction("elx_allocate_function", allocFuncTy);

  llvm::FunctionType *callFuncTy = llvm::FunctionType::get(
      llvmValueTy(),
      {llvmValueTy(), llvm::PointerType::get(llvmValueTy(), 0),
       llvm::Type::getInt32Ty(ctx)},
      false);
  mod.getOrInsertFunction("elx_call_function", callFuncTy);

  // Built-ins will be initialized when first generating code
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
  // Convert i1 to Value representation
  // Boolean values are: QNAN | (Tag::BOOL << 48) | (0 or 1)
  auto qnanVal =
      llvm::ConstantInt::get(llvmValueTy(), 0x7ff8000000000000ULL); // QNAN
  auto tagVal = llvm::ConstantInt::get(
      llvmValueTy(), (static_cast<uint64_t>(Tag::BOOL) << 48));
  auto extended = builder.CreateZExt(i1, llvmValueTy(), "extend");
  auto withTag = builder.CreateOr(qnanVal, tagVal, "qnan_tag");
  return builder.CreateOr(withTag, extended, "bool");
}

llvm::Value *CodeGenVisitor::isString(llvm::Value *v) {
  auto objTag = llvm::ConstantInt::get(llvmValueTy(),
                                       (static_cast<uint64_t>(Tag::OBJ) << 48));
  auto tag = tagOf(v);
  return builder.CreateICmpEQ(tag, objTag, "isstr");
}

llvm::Value *CodeGenVisitor::stringConst(const std::string &str) {
  // Check if this string has already been interned
  auto internIt = internedStrings.find(str);
  if (internIt != internedStrings.end()) {
    return internIt->second; // Return existing string object
  }

  // Create a new string object
  auto strConstant = builder.CreateGlobalStringPtr(str, "str");
  auto lengthConst =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), str.length());

  // Call elx_allocate_string
  auto allocFn = mod.getFunction("elx_allocate_string");
  if (!allocFn) {
    // Fallback to nil if function not found
    return nilConst();
  }

  auto strObj =
      builder.CreateCall(allocFn, {strConstant, lengthConst}, "strobj");

  // Intern the string for future use
  internedStrings[str] = strObj;

  return strObj;
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
  builder.CreateBr(contBB);

  // Same types - compare based on type
  builder.SetInsertPoint(sameTypeBB);
  auto numTag = llvm::ConstantInt::get(
      llvmValueTy(), static_cast<uint64_t>(Tag::NUMBER) << 48);
  auto objTag = llvm::ConstantInt::get(llvmValueTy(),
                                       static_cast<uint64_t>(Tag::OBJ) << 48);

  auto isNumBB = llvm::BasicBlock::Create(ctx, "eq.num", fn);
  auto isObjBB = llvm::BasicBlock::Create(ctx, "eq.obj", fn);
  auto isBoolOrNilBB = llvm::BasicBlock::Create(ctx, "eq.boolnil", fn);
  auto checkObjBB = llvm::BasicBlock::Create(ctx, "check_obj", fn);

  auto isNum = builder.CreateICmpEQ(tagL, numTag, "isnum");
  auto isObj = builder.CreateICmpEQ(tagL, objTag, "isobj");

  // Create a switch-like structure
  builder.CreateCondBr(isNum, isNumBB, checkObjBB);

  // Check obj branch
  builder.SetInsertPoint(checkObjBB);
  builder.CreateCondBr(isObj, isObjBB, isBoolOrNilBB);

  // Numbers: use floating-point comparison (handles NaN correctly)
  builder.SetInsertPoint(isNumBB);
  auto Ld = toDouble(L);
  auto Rd = toDouble(R);
  auto numEqual = builder.CreateFCmpOEQ(Ld, Rd, "numeq");
  builder.CreateBr(contBB);

  // Objects: call string comparison function
  builder.SetInsertPoint(isObjBB);
  auto strEqualFn = mod.getFunction("elx_strings_equal");
  auto objEqual = builder.CreateCall(strEqualFn, {L, R}, "streq");
  auto objEqualBool = builder.CreateICmpNE(
      objEqual, llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0),
      "streqbool");
  builder.CreateBr(contBB);

  // For bool/nil, do bitwise comparison
  builder.SetInsertPoint(isBoolOrNilBB);
  auto bitsEqual = builder.CreateICmpEQ(L, R, "bitseq");
  builder.CreateBr(contBB);

  // Merge results
  builder.SetInsertPoint(contBB);
  auto phi = builder.CreatePHI(builder.getInt1Ty(), 4, "eq.res");
  phi->addIncoming(builder.getFalse(), diffTypeBB);
  phi->addIncoming(numEqual, isNumBB);
  phi->addIncoming(objEqualBool, isObjBB);
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

  // Error case - generate runtime error instead of trap
  builder.SetInsertPoint(errorBB);
  // Print error message and continue with nil instead of crashing
  auto printFn = mod.getFunction("elx_print");
  if (printFn) {
    auto errorMsg = stringConst("Runtime error: Operands must be numbers.");
    builder.CreateCall(printFn, {errorMsg}, "print_error");
  }
  // Don't return here - let the caller handle control flow

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

  // Handle PLUS specially - it can be number addition or string concatenation
  if (e->op.getType() == TokenType::PLUS) {
    auto bothAreNumbers =
        builder.CreateAnd(isNumber(L), isNumber(R), "bothnum");
    auto bothAreObjects =
        builder.CreateAnd(isString(L), isString(R), "bothstr");

    auto isNumAddBB = llvm::BasicBlock::Create(ctx, "plus.numadd", fn);
    auto isStrConcatBB = llvm::BasicBlock::Create(ctx, "plus.strconcat", fn);
    auto errorBB = llvm::BasicBlock::Create(ctx, "plus.error", fn);
    auto contBB = llvm::BasicBlock::Create(ctx, "plus.cont", fn);

    // Check if both are numbers first
    auto checkStrBB = llvm::BasicBlock::Create(ctx, "plus.checkstr", fn);
    builder.CreateCondBr(bothAreNumbers, isNumAddBB, checkStrBB);

    // Check if both are strings
    builder.SetInsertPoint(checkStrBB);
    builder.CreateCondBr(bothAreObjects, isStrConcatBB, errorBB);

    // Number addition
    builder.SetInsertPoint(isNumAddBB);
    auto Ld = toDouble(L);
    auto Rd = toDouble(R);
    auto numResult = fromDouble(builder.CreateFAdd(Ld, Rd, "add"));
    builder.CreateBr(contBB);

    // String concatenation
    builder.SetInsertPoint(isStrConcatBB);
    auto concatFn = mod.getFunction("elx_concatenate_strings");
    auto strResult = builder.CreateCall(concatFn, {L, R}, "concat");
    builder.CreateBr(contBB);

    // Type error
    builder.SetInsertPoint(errorBB);
    // Print error message and continue with nil instead of crashing
    auto printFn = mod.getFunction("elx_print");
    if (printFn) {
      auto errorMsg = stringConst(
          "Runtime error: Operands must be numbers or strings for +.");
      builder.CreateCall(printFn, {errorMsg}, "print_error");
    }
    auto errorResult = nilConst();
    builder.CreateBr(contBB);

    // Merge results
    builder.SetInsertPoint(contBB);
    auto phi = builder.CreatePHI(llvmValueTy(), 3, "plus.res");
    phi->addIncoming(numResult, isNumAddBB);
    phi->addIncoming(strResult, isStrConcatBB);
    phi->addIncoming(errorResult, errorBB);
    value = phi;
    return;
  }

  // For other arithmetic and ordering operators, both operands must be numbers
  llvm::BasicBlock *bothNumBB;
  llvm::BasicBlock *errorBB;
  checkBothNumbers(L, R, bothNumBB, errorBB);

  // Fast path: both are numbers
  builder.SetInsertPoint(bothNumBB);
  llvm::Value *Ld = toDouble(L);
  llvm::Value *Rd = toDouble(R);
  llvm::Value *res = nullptr;

  switch (e->op.getType()) {
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

  auto contBB = llvm::BasicBlock::Create(ctx, "binop.cont",
                                         builder.GetInsertBlock()->getParent());
  builder.CreateBr(contBB);

  // Handle error case
  builder.SetInsertPoint(errorBB);
  auto errorResult = nilConst();
  builder.CreateBr(contBB);

  // Merge results
  builder.SetInsertPoint(contBB);
  auto phi = builder.CreatePHI(llvmValueTy(), 2, "binop.res");
  phi->addIncoming(res, bothNumBB);
  phi->addIncoming(errorResult, errorBB);
  value = phi;
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
    // Create a string object
    const std::string &str = std::get<std::string>(e->value);
    value = stringConst(str);
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

    auto isNum = isNumber(R);
    builder.CreateCondBr(isNum, isNumBB, slowBB);

    builder.SetInsertPoint(isNumBB);
    auto d = toDouble(R);
    auto nd = builder.CreateFNeg(d, "neg");
    auto rv = fromDouble(nd);
    builder.CreateBr(contBB);

    builder.SetInsertPoint(slowBB);
    // Print error message and continue with nil instead of crashing
    auto printFn = mod.getFunction("elx_print");
    if (printFn) {
      auto errorMsg =
          stringConst("Runtime error: Operand must be a number for negation.");
      builder.CreateCall(printFn, {errorMsg}, "print_error");
    }
    auto errorResult = nilConst();
    builder.CreateBr(contBB);

    builder.SetInsertPoint(contBB);
    auto phi = builder.CreatePHI(llvmValueTy(), 2, "neg.res");
    phi->addIncoming(rv, isNumBB);
    phi->addIncoming(errorResult, slowBB);
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
  const std::string &varName = e->name.getLexeme();

  // Initialize built-ins if not already done
  if (!builtinsInitialized) {
    initializeBuiltins();
    builtinsInitialized = true;
  }

  // Check locals first
  auto it = locals.find(varName);
  if (it != locals.end()) {
    // Check if this is a direct value (like a parameter) or needs to be loaded
    if (directValues.count(varName)) {
      value = it->second; // Direct value, no load needed
    } else {
      value = builder.CreateLoad(llvmValueTy(), it->second, varName.c_str());
    }
    return;
  }

  // Check globals
  auto globalIt = globals.find(varName);
  if (globalIt != globals.end()) {
    value = globalIt->second;
    return;
  }

  // Not found - emit runtime error
  auto printFn = mod.getFunction("elx_print");
  std::string errorMsg = "Runtime error: Undefined variable '" + varName + "'.";
  auto msgValue = stringConst(errorMsg);
  builder.CreateCall(printFn, {msgValue});
  value = nilConst();
}

void CodeGenVisitor::visitAssignExpr(Assign *e) {
  e->value->accept(this);
  llvm::Value *assignValue = value;

  const std::string &varName = e->name.getLexeme();

  // Check locals first
  auto localIt = locals.find(varName);
  if (localIt != locals.end()) {
    if (directValues.count(varName)) {
      // This is a parameter or direct value - we need to create storage for it
      auto fn = builder.GetInsertBlock()->getParent();
      auto &entry = fn->getEntryBlock();
      llvm::IRBuilder<> save(entry.getFirstNonPHI());
      auto slot = save.CreateAlloca(llvmValueTy(), nullptr, varName.c_str());
      locals[varName] = slot;
      directValues.erase(varName);
    }
    builder.CreateStore(assignValue, localIt->second);
    value = assignValue; // Assignment returns the assigned value
    return;
  }

  // Check if it's a global variable
  auto globalIt = globals.find(varName);
  if (globalIt != globals.end()) {
    // For globals, we update the globals map
    globals[varName] = assignValue;
    value = assignValue;
    return;
  }

  // Variable not found - this is an error in Lox
  auto printFn = mod.getFunction("elx_print");
  if (printFn) {
    auto errorMsg =
        stringConst("Runtime error: Undefined variable '" + varName + "'.");
    builder.CreateCall(printFn, {errorMsg}, "print_error");
  }
  value = nilConst();
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
  // Evaluate the callee expression
  e->callee->accept(this);
  llvm::Value *callee = value;

  // Evaluate all arguments
  std::vector<llvm::Value *> args;
  for (auto &arg : e->arguments) {
    arg->accept(this);
    args.push_back(value);
  }

  // Create an array to pass arguments to runtime function
  llvm::Function *callFn = mod.getFunction("elx_call_function");
  if (!callFn) {
    value = nilConst();
    return;
  }

  if (args.empty()) {
    // No arguments case
    llvm::Value *nullPtr = llvm::ConstantPointerNull::get(
        llvm::PointerType::get(llvmValueTy(), 0));
    llvm::Value *argCount =
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0);
    value = builder.CreateCall(callFn, {callee, nullPtr, argCount});
  } else {
    // Create an array on the stack for arguments
    llvm::Value *argArray = builder.CreateAlloca(
        llvmValueTy(),
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), args.size()),
        "args");

    // Store each argument in the array
    for (size_t i = 0; i < args.size(); ++i) {
      llvm::Value *idx = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), i);
      llvm::Value *elemPtr = builder.CreateGEP(llvmValueTy(), argArray, idx);
      builder.CreateStore(args[i], elemPtr);
    }

    llvm::Value *argCount =
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), args.size());
    value = builder.CreateCall(callFn, {callee, argArray, argCount});
  }
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

  // Create alloca at the beginning of the entry block
  auto insertPoint = entry.getFirstNonPHI();
  llvm::IRBuilder<> save(&entry, insertPoint ? insertPoint->getIterator()
                                             : entry.begin());
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

void CodeGenVisitor::visitFunctionStmt(Function *s) {
  const std::string &baseFuncName = s->name.getLexeme();
  int arity = s->params.size();

  // Make function names unique to avoid JIT symbol conflicts
  static int functionCounter = 0;
  std::string funcName =
      baseFuncName + "_fn" + std::to_string(functionCounter++);

  // Create function type (all parameters and return value are Value types)
  std::vector<llvm::Type *> paramTypes(arity, llvmValueTy());
  llvm::FunctionType *funcType =
      llvm::FunctionType::get(llvmValueTy(), paramTypes, false);

  // Create the LLVM function
  llvm::Function *llvmFunc = llvm::Function::Create(
      funcType, llvm::Function::ExternalLinkage, funcName, &mod);

  // Store in function table using the unique name
  functions[baseFuncName] = llvmFunc;

  // Save current state
  llvm::Function *prevFunction = currentFunction;
  auto prevLocals = locals;
  llvm::BasicBlock *prevBB = builder.GetInsertBlock();

  // Set up new function
  currentFunction = llvmFunc;
  locals.clear();
  directValues.clear();

  // Create entry block
  llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(ctx, "entry", llvmFunc);
  builder.SetInsertPoint(entryBB);

  // Add parameters to local scope as direct values
  auto argIt = llvmFunc->arg_begin();
  for (size_t i = 0; i < s->params.size(); ++i, ++argIt) {
    const std::string &paramName = s->params[i].getLexeme();
    argIt->setName(paramName);
    locals[paramName] = &*argIt;
    directValues.insert(paramName); // Mark as direct value
  }

  // Generate function body
  s->body->accept(this);

  // If no explicit return, return nil
  if (!builder.GetInsertBlock()->getTerminator()) {
    builder.CreateRet(nilConst());
  }

  // Verify the function
  if (llvm::verifyFunction(*llvmFunc, &llvm::errs())) {
    llvm::errs() << "Function verification failed for: " << baseFuncName
                 << "\n";
    llvmFunc->eraseFromParent();
    functions.erase(baseFuncName);
  }

  // Restore previous state
  currentFunction = prevFunction;
  locals = std::move(prevLocals);
  if (prevBB) {
    builder.SetInsertPoint(prevBB);
  }

  // Create a function object value and store it in the variable with the
  // function name (use original name, not unique name)
  auto nameStr = builder.CreateGlobalStringPtr(baseFuncName, "fname");
  auto arityConst = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), arity);
  auto funcPtr = builder.CreateBitCast(
      llvmFunc, llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0));

  auto allocFn = mod.getFunction("elx_allocate_function");
  auto funcObj =
      builder.CreateCall(allocFn, {nameStr, arityConst, funcPtr}, "funcobj");

  // Store the function object in globals for access across expressions
  // Use the original function name for lookup
  globals[baseFuncName] = funcObj;

  // Also create a local variable for the current scope if we're in a function
  if (currentFunction && builder.GetInsertBlock()) {
    auto fn = builder.GetInsertBlock()->getParent();
    auto &entry = fn->getEntryBlock();
    llvm::IRBuilder<> entryBuilder(entry.getFirstNonPHI());
    auto slot =
        entryBuilder.CreateAlloca(llvmValueTy(), nullptr, baseFuncName.c_str());
    builder.CreateStore(funcObj, slot);
    locals[baseFuncName] = slot;
  } else {
    // At global scope, store directly
    locals[baseFuncName] = funcObj;
    directValues.insert(baseFuncName);
  }

  value = funcObj;
}
void CodeGenVisitor::visitReturnStmt(Return *s) {
  if (s->value) {
    s->value->accept(this);
    builder.CreateRet(value);
  } else {
    builder.CreateRet(nilConst());
  }
}
void CodeGenVisitor::visitGetExpr(Get *e) { value = nilConst(); }
void CodeGenVisitor::visitSetExpr(Set *e) { value = nilConst(); }
void CodeGenVisitor::visitThisExpr(This *e) { value = nilConst(); }
void CodeGenVisitor::visitSuperExpr(Super *e) { value = nilConst(); }
void CodeGenVisitor::visitClassStmt(Class *s) {}

void CodeGenVisitor::initializeBuiltins() {
  // Create built-in function objects

  // clock() function
  auto clockNameStr = builder.CreateGlobalStringPtr("clock", "clock_name");
  auto clockArityConst = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0);

  // Get the clock runtime function
  auto clockRuntimeFn = mod.getFunction("elx_clock");
  auto clockPtr = builder.CreateBitCast(
      clockRuntimeFn, llvm::PointerType::get(llvm::Type::getInt8Ty(ctx), 0));

  auto allocFn = mod.getFunction("elx_allocate_function");
  auto clockObj = builder.CreateCall(
      allocFn, {clockNameStr, clockArityConst, clockPtr}, "clock_obj");

  // Store in globals
  globals["clock"] = clockObj;
}

} // namespace eloxir
