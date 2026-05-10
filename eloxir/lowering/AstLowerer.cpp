#include "AstLowerer.h"

#include "../frontend/Visitor.h"

#include <stdexcept>
#include <unordered_map>

namespace eloxir::loxir {

namespace {

BinaryOp toBinaryOp(TokenType type) {
  switch (type) {
  case TokenType::PLUS:
    return BinaryOp::Add;
  case TokenType::MINUS:
    return BinaryOp::Subtract;
  case TokenType::STAR:
    return BinaryOp::Multiply;
  case TokenType::SLASH:
    return BinaryOp::Divide;
  case TokenType::EQUAL_EQUAL:
    return BinaryOp::Equal;
  case TokenType::BANG_EQUAL:
    return BinaryOp::NotEqual;
  case TokenType::GREATER:
    return BinaryOp::Greater;
  case TokenType::GREATER_EQUAL:
    return BinaryOp::GreaterEqual;
  case TokenType::LESS:
    return BinaryOp::Less;
  case TokenType::LESS_EQUAL:
    return BinaryOp::LessEqual;
  default:
    return BinaryOp::Add;
  }
}

UnaryOp toUnaryOp(TokenType type) {
  return type == TokenType::MINUS ? UnaryOp::Negate : UnaryOp::Not;
}

SourceLocation sourceFromToken(const Token &token) {
  return SourceLocation{token.getLine()};
}

class FunctionLowerer final : public ExprVisitor, public StmtVisitor {
public:
  FunctionLowerer(LoxModule &module, LoxFunction &function,
                  const std::unordered_map<const Expr *, int> *resolvedLocals,
                  int functionDepth)
      : module_(module), function_(function), resolvedLocals_(resolvedLocals),
        functionDepth_(functionDepth) {
    currentBlock_ = function_.addBlock("entry").id();
  }

  void lower(const std::vector<std::unique_ptr<Stmt>> &statements) {
    for (const auto &statement : statements) {
      if (hasTerminator()) {
        break;
      }
      statement->accept(this);
    }
    if (!hasTerminator()) {
      emitNilReturn();
    }
  }

  void visitBinaryExpr(Binary *expr) override {
    expr->left->accept(this);
    ValueId left = value_;
    expr->right->accept(this);
    ValueId right = value_;

    Instruction instruction;
    instruction.kind = InstructionKind::Binary;
    instruction.source = sourceFromToken(expr->op);
    instruction.result = makeValue();
    instruction.resultType = LoxType::Unknown;
    instruction.binaryOp = toBinaryOp(expr->op.getType());
    instruction.operands = {left, right};
    append(std::move(instruction));
  }

  void visitGroupingExpr(Grouping *expr) override {
    expr->expression->accept(this);
  }

  void visitLiteralExpr(Literal *expr) override {
    Instruction instruction;
    instruction.result = makeValue();
    if (std::holds_alternative<std::monostate>(expr->value)) {
      instruction.kind = InstructionKind::ConstantNil;
      instruction.resultType = LoxType::Nil;
    } else if (const auto *number = std::get_if<double>(&expr->value)) {
      instruction.kind = InstructionKind::ConstantNumber;
      instruction.resultType = LoxType::Number;
      instruction.numberValue = *number;
    } else if (const auto *text = std::get_if<std::string>(&expr->value)) {
      instruction.kind = InstructionKind::ConstantString;
      instruction.resultType = LoxType::String;
      instruction.symbol = *text;
    } else if (const auto *boolean = std::get_if<bool>(&expr->value)) {
      instruction.kind = InstructionKind::ConstantBool;
      instruction.resultType = LoxType::Bool;
      instruction.boolValue = *boolean;
    }
    append(std::move(instruction));
  }

  void visitUnaryExpr(Unary *expr) override {
    expr->right->accept(this);
    ValueId operand = value_;
    Instruction instruction;
    instruction.kind = InstructionKind::Unary;
    instruction.source = sourceFromToken(expr->op);
    instruction.result = makeValue();
    instruction.unaryOp = toUnaryOp(expr->op.getType());
    instruction.operands = {operand};
    append(std::move(instruction));
  }

  void visitVariableExpr(Variable *expr) override {
    Instruction instruction;
    instruction.kind = isResolvedLocal(expr) ? InstructionKind::LoadLocal
                                             : InstructionKind::LoadGlobal;
    instruction.source = sourceFromToken(expr->name);
    instruction.result = makeValue();
    instruction.symbol = instruction.kind == InstructionKind::LoadLocal
                             ? lookupLocal(expr->name.getLexeme())
                             : expr->name.getLexeme();
    append(std::move(instruction));
  }

  void visitAssignExpr(Assign *expr) override {
    expr->value->accept(this);
    ValueId assigned = value_;

    Instruction instruction;
    instruction.kind = isResolvedLocal(expr) ? InstructionKind::StoreLocal
                                             : InstructionKind::StoreGlobal;
    instruction.source = sourceFromToken(expr->name);
    instruction.symbol = instruction.kind == InstructionKind::StoreLocal
                             ? lookupLocal(expr->name.getLexeme())
                             : expr->name.getLexeme();
    instruction.operands = {assigned};
    append(std::move(instruction));
  }

  void visitLogicalExpr(Logical *expr) override {
    expr->left->accept(this);
    ValueId left = value_;

    Instruction truthy;
    truthy.kind = InstructionKind::IsTruthy;
    truthy.source = sourceFromToken(expr->op);
    truthy.result = makeValue();
    truthy.resultType = LoxType::Bool;
    truthy.operands = {left};
    append(std::move(truthy));
    ValueId condition = value_;

    BlockId rightBlock = function_.addBlock("logic.right").id();
    BlockId endBlock = function_.addBlock("logic.end").id();
    Instruction branch;
    branch.kind = InstructionKind::Branch;
    branch.source = sourceFromToken(expr->op);
    branch.operands = {condition};
    if (expr->op.getType() == TokenType::OR) {
      branch.target = endBlock;
      branch.falseTarget = rightBlock;
    } else {
      branch.target = rightBlock;
      branch.falseTarget = endBlock;
    }
    append(std::move(branch));

    currentBlock_ = rightBlock;
    expr->right->accept(this);
    ValueId right = value_;
    emitJump(endBlock);

    currentBlock_ = endBlock;
    Instruction phi;
    phi.kind = InstructionKind::Phi;
    phi.result = makeValue();
    phi.operands = {left, right};
    append(std::move(phi));
  }

  void visitCallExpr(Call *expr) override {
    expr->callee->accept(this);
    ValueId callee = value_;

    std::vector<ValueId> args;
    args.reserve(expr->arguments.size());
    for (const auto &argument : expr->arguments) {
      argument->accept(this);
      args.push_back(value_);
    }

    Instruction instruction;
    instruction.kind = InstructionKind::Call;
    instruction.source = sourceFromToken(expr->paren);
    instruction.result = makeValue();
    instruction.operands = {callee};
    instruction.arguments = std::move(args);
    append(std::move(instruction));
  }

  void visitGetExpr(Get *expr) override {
    expr->object->accept(this);
    Instruction instruction;
    instruction.kind = InstructionKind::GetProperty;
    instruction.source = sourceFromToken(expr->name);
    instruction.result = makeValue();
    instruction.symbol = expr->name.getLexeme();
    instruction.operands = {value_};
    append(std::move(instruction));
  }

  void visitSetExpr(Set *expr) override {
    expr->object->accept(this);
    ValueId object = value_;
    expr->value->accept(this);
    ValueId assigned = value_;

    Instruction instruction;
    instruction.kind = InstructionKind::SetProperty;
    instruction.source = sourceFromToken(expr->name);
    instruction.symbol = expr->name.getLexeme();
    instruction.operands = {object, assigned};
    append(std::move(instruction));
  }

  void visitThisExpr(This *expr) override {
    Instruction instruction;
    instruction.kind = InstructionKind::LoadLocal;
    instruction.source = sourceFromToken(expr->keyword);
    instruction.result = makeValue();
    instruction.symbol = "this";
    append(std::move(instruction));
  }

  void visitSuperExpr(Super *expr) override {
    Instruction loadThis;
    loadThis.kind = InstructionKind::LoadLocal;
    loadThis.source = sourceFromToken(expr->keyword);
    loadThis.result = makeValue();
    loadThis.symbol = "this";
    append(std::move(loadThis));
    ValueId thisValue = value_;

    Instruction loadSuper;
    loadSuper.kind = InstructionKind::LoadLocal;
    loadSuper.source = sourceFromToken(expr->keyword);
    loadSuper.result = makeValue();
    loadSuper.symbol = "super";
    append(std::move(loadSuper));
    ValueId superValue = value_;

    Instruction instruction;
    instruction.kind = InstructionKind::BindSuper;
    instruction.source = sourceFromToken(expr->method);
    instruction.result = makeValue();
    instruction.symbol = expr->method.getLexeme();
    instruction.operands = {superValue, thisValue};
    append(std::move(instruction));
  }

  void visitExpressionStmt(Expression *stmt) override {
    stmt->expression->accept(this);
  }

  void visitPrintStmt(Print *stmt) override {
    stmt->expression->accept(this);
    Instruction instruction;
    instruction.kind = InstructionKind::Print;
    instruction.operands = {value_};
    append(std::move(instruction));
  }

  void visitVarStmt(Var *stmt) override {
    if (stmt->initializer) {
      stmt->initializer->accept(this);
    } else {
      emitNil();
    }
    ValueId initial = value_;

    Instruction instruction;
    bool topLevel = isTopLevel();
    instruction.kind =
        topLevel ? InstructionKind::StoreGlobal : InstructionKind::StoreLocal;
    instruction.source = sourceFromToken(stmt->name);
    instruction.symbol = topLevel ? stmt->name.getLexeme()
                                  : declareLocal(stmt->name.getLexeme());
    instruction.operands = {initial};
    append(std::move(instruction));
  }

  void visitBlockStmt(Block *stmt) override {
    ++blockDepth_;
    pushScope();
    for (const auto &inner : stmt->statements) {
      if (hasTerminator()) {
        break;
      }
      inner->accept(this);
    }
    popScope();
    --blockDepth_;
  }

  void visitIfStmt(If *stmt) override {
    stmt->condition->accept(this);
    ValueId conditionValue = value_;
    Instruction truthy;
    truthy.kind = InstructionKind::IsTruthy;
    truthy.result = makeValue();
    truthy.resultType = LoxType::Bool;
    truthy.operands = {conditionValue};
    append(std::move(truthy));

    BlockId thenBlock = function_.addBlock("if.then").id();
    BlockId elseBlock = function_.addBlock("if.else").id();
    BlockId endBlock = function_.addBlock("if.end").id();
    Instruction branch;
    branch.kind = InstructionKind::Branch;
    branch.operands = {value_};
    branch.target = thenBlock;
    branch.falseTarget = elseBlock;
    append(std::move(branch));

    currentBlock_ = thenBlock;
    stmt->thenBranch->accept(this);
    if (!hasTerminator()) {
      emitJump(endBlock);
    }

    currentBlock_ = elseBlock;
    if (stmt->elseBranch) {
      stmt->elseBranch->accept(this);
    }
    if (!hasTerminator()) {
      emitJump(endBlock);
    }

    currentBlock_ = endBlock;
  }

  void visitWhileStmt(While *stmt) override {
    BlockId conditionBlock = function_.addBlock("while.cond").id();
    BlockId bodyBlock = function_.addBlock("while.body").id();
    BlockId endBlock = function_.addBlock("while.end").id();
    emitJump(conditionBlock);

    currentBlock_ = conditionBlock;
    stmt->condition->accept(this);
    ValueId conditionValue = value_;
    Instruction truthy;
    truthy.kind = InstructionKind::IsTruthy;
    truthy.result = makeValue();
    truthy.resultType = LoxType::Bool;
    truthy.operands = {conditionValue};
    append(std::move(truthy));

    Instruction branch;
    branch.kind = InstructionKind::Branch;
    branch.operands = {value_};
    branch.target = bodyBlock;
    branch.falseTarget = endBlock;
    append(std::move(branch));

    currentBlock_ = bodyBlock;
    stmt->body->accept(this);
    if (!hasTerminator()) {
      emitJump(conditionBlock);
    }

    currentBlock_ = endBlock;
  }

  void visitFunctionStmt(Function *stmt) override {
    LoxFunction &child = module_.addFunction(stmt->name.getLexeme());
    FunctionLowerer childLowerer(module_, child, resolvedLocals_,
                                 functionDepth_ + 1);
    for (const Token &param : stmt->params) {
      child.addParameter(param.getLexeme());
    }
    if (stmt->body) {
      childLowerer.lower(stmt->body->statements);
    }

    Instruction instruction;
    instruction.kind = InstructionKind::DefineFunction;
    instruction.source = sourceFromToken(stmt->name);
    instruction.result = makeValue();
    instruction.resultType = LoxType::Function;
    instruction.symbol = stmt->name.getLexeme();
    append(std::move(instruction));

    Instruction store;
    bool topLevel = isTopLevel();
    store.kind = topLevel ? InstructionKind::StoreGlobal
                          : InstructionKind::StoreLocal;
    store.source = sourceFromToken(stmt->name);
    store.symbol = topLevel ? stmt->name.getLexeme()
                            : declareLocal(stmt->name.getLexeme());
    store.operands = {value_};
    append(std::move(store));
  }

  void visitReturnStmt(Return *stmt) override {
    if (stmt->value) {
      stmt->value->accept(this);
    } else {
      emitNil();
    }
    Instruction instruction;
    instruction.kind = InstructionKind::Return;
    instruction.source = sourceFromToken(stmt->keyword);
    instruction.operands = {value_};
    append(std::move(instruction));
  }

  void visitClassStmt(Class *stmt) override {
    std::vector<ValueId> operands;
    if (stmt->superclass) {
      stmt->superclass->accept(this);
      operands.push_back(value_);
    }

    Instruction classInstruction;
    classInstruction.kind = InstructionKind::DefineClass;
    classInstruction.source = sourceFromToken(stmt->name);
    classInstruction.result = makeValue();
    classInstruction.resultType = LoxType::Class;
    classInstruction.symbol = stmt->name.getLexeme();
    classInstruction.operands = operands;
    append(std::move(classInstruction));
    ValueId classValue = value_;

    Instruction store;
    bool topLevel = isTopLevel();
    store.kind = topLevel ? InstructionKind::StoreGlobal
                          : InstructionKind::StoreLocal;
    store.source = sourceFromToken(stmt->name);
    store.symbol = topLevel ? stmt->name.getLexeme()
                            : declareLocal(stmt->name.getLexeme());
    store.operands = {classValue};
    append(std::move(store));

    for (const auto &method : stmt->methods) {
      LoxFunction &methodFn = module_.addFunction(stmt->name.getLexeme() +
                                                  "::" +
                                                  method->name.getLexeme());
      methodFn.addParameter("this");
      for (const Token &param : method->params) {
        methodFn.addParameter(param.getLexeme());
      }
      FunctionLowerer methodLowerer(module_, methodFn, resolvedLocals_,
                                    functionDepth_ + 1);
      if (method->body) {
        methodLowerer.lower(method->body->statements);
      }

      Instruction fnObject;
      fnObject.kind = InstructionKind::DefineFunction;
      fnObject.source = sourceFromToken(method->name);
      fnObject.result = makeValue();
      fnObject.resultType = LoxType::Function;
      fnObject.symbol = methodFn.name();
      append(std::move(fnObject));
      ValueId methodValue = value_;

      Instruction defineMethod;
      defineMethod.kind = InstructionKind::DefineMethod;
      defineMethod.source = sourceFromToken(method->name);
      defineMethod.symbol = method->name.getLexeme();
      defineMethod.operands = {classValue, methodValue};
      append(std::move(defineMethod));
    }
  }

private:
  LoxModule &module_;
  LoxFunction &function_;
  const std::unordered_map<const Expr *, int> *resolvedLocals_;
  int functionDepth_ = 0;
  int blockDepth_ = 0;
  BlockId currentBlock_;
  ValueId value_;
  std::vector<std::unordered_map<std::string, std::string>> localScopes_;
  uint32_t nextLocalId_ = 0;

  bool isTopLevel() const {
    return functionDepth_ == 0 && blockDepth_ == 0;
  }

  bool isResolvedLocal(const Expr *expr) const {
    return resolvedLocals_ && resolvedLocals_->find(expr) != resolvedLocals_->end();
  }

  void pushScope() { localScopes_.emplace_back(); }

  void popScope() {
    if (!localScopes_.empty()) {
      localScopes_.pop_back();
    }
  }

  std::string declareLocal(const std::string &name) {
    if (localScopes_.empty()) {
      pushScope();
    }
    std::string symbol = name + "$" + std::to_string(nextLocalId_++);
    localScopes_.back()[name] = symbol;
    return symbol;
  }

  std::string lookupLocal(const std::string &name) const {
    for (auto scope = localScopes_.rbegin(); scope != localScopes_.rend();
         ++scope) {
      auto it = scope->find(name);
      if (it != scope->end()) {
        return it->second;
      }
    }
    return name;
  }

  bool hasTerminator() { return block().hasTerminator(); }

  BasicBlock &block() {
    BasicBlock *bb = function_.findBlock(currentBlock_);
    if (!bb) {
      throw std::runtime_error("invalid LoxIR insertion block");
    }
    return *bb;
  }

  ValueId makeValue() {
    value_ = function_.makeValue();
    return value_;
  }

  void append(Instruction instruction) {
    if (instruction.result) {
      value_ = *instruction.result;
    }
    block().append(std::move(instruction));
  }

  void emitNil() {
    Instruction instruction;
    instruction.kind = InstructionKind::ConstantNil;
    instruction.result = makeValue();
    instruction.resultType = LoxType::Nil;
    append(std::move(instruction));
  }

  void emitNilReturn() {
    emitNil();
    Instruction instruction;
    instruction.kind = InstructionKind::Return;
    instruction.operands = {value_};
    append(std::move(instruction));
  }

  void emitJump(BlockId target) {
    Instruction instruction;
    instruction.kind = InstructionKind::Jump;
    instruction.target = target;
    append(std::move(instruction));
  }
};

} // namespace

AstLowerer::AstLowerer(
    const std::unordered_map<const Expr *, int> *resolvedLocals)
    : resolvedLocals_(resolvedLocals) {}

LoxModule
AstLowerer::lower(const std::string &moduleName,
                  const std::vector<std::unique_ptr<Stmt>> &statements) {
  LoxModule module(moduleName);
  LoxFunction &main = module.addFunction("main");
  FunctionLowerer lowerer(module, main, resolvedLocals_, 0);
  lowerer.lower(statements);
  return module;
}

} // namespace eloxir::loxir
