#include "AstLowerer.h"

#include "../frontend/CompileError.h"
#include "../frontend/Visitor.h"

#include <algorithm>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

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
                  const std::unordered_map<const Function *,
                                           std::vector<std::string>>
                      *functionUpvalues,
                  int functionDepth, uint32_t &functionCounter,
                  const std::string &symbolPrefix)
      : module_(module), function_(function), resolvedLocals_(resolvedLocals),
        functionUpvalues_(functionUpvalues), functionDepth_(functionDepth),
        functionCounter_(functionCounter), symbolPrefix_(symbolPrefix) {
    currentBlock_ = function_.addBlock("entry").id();
    pushScope();
    for (const auto &parameter : function_.parameters()) {
      localScopes_.back()[parameter.name] = parameter.name;
    }
    for (uint32_t index = 0; index < function_.upvalues().size(); ++index) {
      upvalueIndices_[function_.upvalues()[index].name] = index;
    }
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
    recordConstant(expr);

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
    auto local = lookupLocalSymbol(expr->name.getLexeme());
    if (local) {
      instruction.kind = InstructionKind::LoadLocal;
      instruction.symbol = *local;
    } else if (isUpvalue(expr->name.getLexeme())) {
      instruction.kind = InstructionKind::LoadUpvalue;
      instruction.symbol = expr->name.getLexeme();
    } else {
      instruction.kind = InstructionKind::LoadGlobal;
      instruction.symbol = expr->name.getLexeme();
    }
    instruction.source = sourceFromToken(expr->name);
    instruction.result = makeValue();
    append(std::move(instruction));
  }

  void visitAssignExpr(Assign *expr) override {
    expr->value->accept(this);
    ValueId assigned = value_;

    Instruction instruction;
    auto local = lookupLocalSymbol(expr->name.getLexeme());
    if (local) {
      instruction.kind = InstructionKind::StoreLocal;
      instruction.symbol = *local;
    } else if (isUpvalue(expr->name.getLexeme())) {
      instruction.kind = InstructionKind::StoreUpvalue;
      instruction.symbol = expr->name.getLexeme();
    } else {
      instruction.kind = InstructionKind::StoreGlobal;
      instruction.symbol = expr->name.getLexeme();
    }
    instruction.source = sourceFromToken(expr->name);
    instruction.operands = {assigned};
    append(std::move(instruction));
  }

  void visitLogicalExpr(Logical *expr) override {
    expr->left->accept(this);
    ValueId left = value_;

    const std::string resultSlot = makeTempLocal("logic");
    Instruction storeLeft;
    storeLeft.kind = InstructionKind::StoreLocal;
    storeLeft.source = sourceFromToken(expr->op);
    storeLeft.symbol = resultSlot;
    storeLeft.declaresSymbol = true;
    storeLeft.operands = {left};
    append(std::move(storeLeft));

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
    Instruction storeRight;
    storeRight.kind = InstructionKind::StoreLocal;
    storeRight.source = sourceFromToken(expr->op);
    storeRight.symbol = resultSlot;
    storeRight.operands = {right};
    append(std::move(storeRight));
    emitJump(endBlock);

    currentBlock_ = endBlock;
    Instruction loadResult;
    loadResult.kind = InstructionKind::LoadLocal;
    loadResult.source = sourceFromToken(expr->op);
    loadResult.result = makeValue();
    loadResult.symbol = resultSlot;
    append(std::move(loadResult));
  }

  void visitCallExpr(Call *expr) override {
    if (auto *get = dynamic_cast<Get *>(expr->callee.get())) {
      get->object->accept(this);
      ValueId receiver = value_;

      ValueId target = makeValue();
      ValueId kind = function_.makeValue();
      Instruction prepare;
      prepare.kind = InstructionKind::PreparePropertyCall;
      prepare.source = sourceFromToken(get->name);
      prepare.result = target;
      prepare.auxResult = kind;
      prepare.symbol = get->name.getLexeme();
      prepare.operands = {receiver};
      append(std::move(prepare));

      std::vector<ValueId> args;
      args.reserve(expr->arguments.size());
      for (const auto &argument : expr->arguments) {
        argument->accept(this);
        args.push_back(value_);
      }

      Instruction call;
      call.kind = InstructionKind::CallPreparedProperty;
      call.source = sourceFromToken(expr->paren);
      call.result = makeValue();
      call.symbol = get->name.getLexeme();
      call.operands = {receiver, kind, target};
      call.arguments = std::move(args);
      append(std::move(call));
      return;
    }

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
    ValueId object = value_;
    Instruction instruction;
    instruction.kind = InstructionKind::GetProperty;
    instruction.source = sourceFromToken(expr->name);
    instruction.result = makeValue();
    instruction.symbol = expr->name.getLexeme();
    instruction.operands = {object};
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
    auto local = lookupLocalSymbol("this");
    instruction.kind =
        local ? InstructionKind::LoadLocal : InstructionKind::LoadUpvalue;
    instruction.source = sourceFromToken(expr->keyword);
    instruction.result = makeValue();
    instruction.symbol = local ? *local : "this";
    append(std::move(instruction));
  }

  void visitSuperExpr(Super *expr) override {
    Instruction loadThis;
    auto thisLocal = lookupLocalSymbol("this");
    loadThis.kind =
        thisLocal ? InstructionKind::LoadLocal : InstructionKind::LoadUpvalue;
    loadThis.source = sourceFromToken(expr->keyword);
    loadThis.result = makeValue();
    loadThis.symbol = thisLocal ? *thisLocal : "this";
    append(std::move(loadThis));
    ValueId thisValue = value_;

    Instruction loadSuper;
    auto superLocal = lookupLocalSymbol("super");
    loadSuper.kind =
        superLocal ? InstructionKind::LoadLocal : InstructionKind::LoadUpvalue;
    loadSuper.source = sourceFromToken(expr->keyword);
    loadSuper.result = makeValue();
    loadSuper.symbol = superLocal ? *superLocal : "super";
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
    instruction.declaresSymbol = true;
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
    const bool topLevel = isTopLevel();
    std::string localSymbol;
    if (!topLevel) {
      localSymbol = declareLocal(stmt->name.getLexeme());
      emitNil();
      Instruction placeholder;
      placeholder.kind = InstructionKind::StoreLocal;
      placeholder.source = sourceFromToken(stmt->name);
      placeholder.symbol = localSymbol;
      placeholder.declaresSymbol = true;
      placeholder.operands = {value_};
      append(std::move(placeholder));
    }

    LoxFunction &child =
        module_.addFunction(uniqueFunctionName(stmt->name.getLexeme()));
    child.setDisplayName(stmt->name.getLexeme());
    child.setArity(static_cast<int>(stmt->params.size()));
    for (const Token &param : stmt->params) {
      child.addParameter(param.getLexeme());
    }
    configureUpvalues(child, stmt, false);

    FunctionLowerer childLowerer(module_, child, resolvedLocals_,
                                 functionUpvalues_, functionDepth_ + 1,
                                 functionCounter_, symbolPrefix_);
    if (stmt->body) {
      childLowerer.lower(stmt->body->statements);
    }

    Instruction instruction;
    instruction.kind = InstructionKind::DefineFunction;
    instruction.source = sourceFromToken(stmt->name);
    instruction.result = makeValue();
    instruction.resultType = LoxType::Function;
    instruction.symbol = child.name();
    append(std::move(instruction));

    Instruction store;
    store.kind = topLevel ? InstructionKind::StoreGlobal
                          : InstructionKind::StoreLocal;
    store.source = sourceFromToken(stmt->name);
    store.symbol = topLevel ? stmt->name.getLexeme() : localSymbol;
    store.declaresSymbol = topLevel;
    store.operands = {value_};
    append(std::move(store));
  }

  void visitReturnStmt(Return *stmt) override {
    if (function_.isInitializer()) {
      if (stmt->value) {
        stmt->value->accept(this);
      }
      emitThisReturn(sourceFromToken(stmt->keyword));
      return;
    }
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
    const bool topLevel = isTopLevel();
    std::string classSymbol;
    if (!topLevel) {
      classSymbol = declareLocal(stmt->name.getLexeme());
      emitNil();
      Instruction placeholder;
      placeholder.kind = InstructionKind::StoreLocal;
      placeholder.source = sourceFromToken(stmt->name);
      placeholder.symbol = classSymbol;
      placeholder.declaresSymbol = true;
      placeholder.operands = {value_};
      append(std::move(placeholder));
    }

    std::vector<ValueId> operands;
    bool hasSuperScope = false;
    if (stmt->superclass) {
      stmt->superclass->accept(this);
      operands.push_back(value_);
      pushScope();
      hasSuperScope = true;
      std::string superSymbol = declareLocal("super");
      Instruction superStore;
      superStore.kind = InstructionKind::StoreLocal;
      superStore.source = sourceFromToken(stmt->name);
      superStore.symbol = superSymbol;
      superStore.declaresSymbol = true;
      superStore.operands = {value_};
      append(std::move(superStore));
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
    store.kind = topLevel ? InstructionKind::StoreGlobal
                          : InstructionKind::StoreLocal;
    store.source = sourceFromToken(stmt->name);
    store.symbol = topLevel ? stmt->name.getLexeme() : classSymbol;
    store.declaresSymbol = topLevel;
    store.operands = {classValue};
    append(std::move(store));

    for (const auto &method : stmt->methods) {
      const bool initializer = method->name.getLexeme() == "init";
      std::string displayName = method->name.getLexeme();
      LoxFunction &methodFn =
          module_.addFunction(uniqueFunctionName(stmt->name.getLexeme() +
                                                 "::" + displayName));
      methodFn.setDisplayName(displayName);
      methodFn.setMethod(true);
      methodFn.setInitializer(initializer);
      methodFn.addParameter("this", LoxType::Instance);
      for (const Token &param : method->params) {
        methodFn.addParameter(param.getLexeme());
      }
      methodFn.setArity(static_cast<int>(methodFn.parameters().size()));
      configureUpvalues(methodFn, method.get(), true);

      FunctionLowerer methodLowerer(module_, methodFn, resolvedLocals_,
                                    functionUpvalues_, functionDepth_ + 1,
                                    functionCounter_, symbolPrefix_);
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

    if (hasSuperScope) {
      popScope();
    }
  }

private:
  LoxModule &module_;
  LoxFunction &function_;
  const std::unordered_map<const Expr *, int> *resolvedLocals_;
  const std::unordered_map<const Function *, std::vector<std::string>>
      *functionUpvalues_;
  int functionDepth_ = 0;
  int blockDepth_ = 0;
  BlockId currentBlock_;
  ValueId value_;
  std::vector<std::unordered_map<std::string, std::string>> localScopes_;
  std::unordered_map<std::string, uint32_t> upvalueIndices_;
  uint32_t nextLocalId_ = 0;
  uint32_t constantCount_ = 0;
  uint32_t &functionCounter_;
  const std::string &symbolPrefix_;
  static constexpr uint32_t kMaxConstants = 256;

  bool isTopLevel() const {
    return functionDepth_ == 0 && blockDepth_ == 0;
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

  std::string makeTempLocal(const std::string &name) {
    return name + "$tmp" + std::to_string(nextLocalId_++);
  }

  std::string lookupLocal(const std::string &name) const {
    auto symbol = lookupLocalSymbol(name);
    return symbol ? *symbol : name;
  }

  std::optional<std::string> lookupLocalSymbol(const std::string &name) const {
    for (auto scope = localScopes_.rbegin(); scope != localScopes_.rend();
         ++scope) {
      auto it = scope->find(name);
      if (it != scope->end()) {
        return it->second;
      }
    }
    return std::nullopt;
  }

  bool isUpvalue(const std::string &name) const {
    return upvalueIndices_.find(name) != upvalueIndices_.end();
  }

  std::string uniqueFunctionName(const std::string &base) {
    return symbolPrefix_ + base + "$fn" + std::to_string(functionCounter_++);
  }

  void recordConstant(const Literal *literal) {
    const bool isPoolConstant = std::holds_alternative<double>(literal->value) ||
                                std::holds_alternative<std::string>(
                                    literal->value);
    if (!isPoolConstant) {
      return;
    }
    if (constantCount_ >= kMaxConstants) {
      throw CompileError("Error at '" + literalLexeme(literal) +
                         "': Too many constants in one chunk.");
    }
    ++constantCount_;
  }

  static std::string literalLexeme(const Literal *literal) {
    if (const auto *number = std::get_if<double>(&literal->value)) {
      std::ostringstream out;
      out.precision(15);
      out << *number;
      return out.str();
    }
    if (const auto *text = std::get_if<std::string>(&literal->value)) {
      return "\"" + *text + "\"";
    }
    return "literal";
  }

  std::vector<std::string> resolverUpvaluesFor(const Function *function,
                                               bool isMethod) const {
    std::vector<std::string> upvalues;
    if (functionUpvalues_) {
      auto it = functionUpvalues_->find(function);
      if (it != functionUpvalues_->end()) {
        upvalues = it->second;
      }
    }

    upvalues.erase(std::remove_if(upvalues.begin(), upvalues.end(),
                                  [&](const std::string &name) {
                                    if (isMethod && name == "this") {
                                      return true;
                                    }
                                    return std::any_of(
                                        function->params.begin(),
                                        function->params.end(),
                                        [&](const Token &param) {
                                          return param.getLexeme() == name;
                                        });
                                  }),
                   upvalues.end());

    if (!isMethod &&
        std::find(upvalues.begin(), upvalues.end(), "super") !=
            upvalues.end() &&
        std::find(upvalues.begin(), upvalues.end(), "this") ==
            upvalues.end()) {
      upvalues.push_back("this");
    }

    std::vector<std::string> unique;
    std::unordered_set<std::string> seen;
    for (const std::string &name : upvalues) {
      if (seen.insert(name).second) {
        unique.push_back(name);
      }
    }
    return unique;
  }

  void configureUpvalues(LoxFunction &child, const Function *function,
                         bool isMethod) const {
    for (const std::string &name : resolverUpvaluesFor(function, isMethod)) {
      Upvalue upvalue;
      upvalue.name = name;
      if (auto local = lookupLocalSymbol(name)) {
        upvalue.source = UpvalueSourceKind::Local;
        upvalue.sourceSymbol = *local;
      } else {
        auto parentUpvalue = upvalueIndices_.find(name);
        if (parentUpvalue == upvalueIndices_.end()) {
          continue;
        }
        upvalue.source = UpvalueSourceKind::Upvalue;
        upvalue.sourceIndex = parentUpvalue->second;
      }
      child.addUpvalue(std::move(upvalue));
    }
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
    if (function_.isInitializer()) {
      emitThisReturn(SourceLocation{});
      return;
    }
    emitNil();
    Instruction instruction;
    instruction.kind = InstructionKind::Return;
    instruction.operands = {value_};
    append(std::move(instruction));
  }

  void emitThisReturn(SourceLocation source) {
    Instruction loadThis;
    loadThis.kind = InstructionKind::LoadLocal;
    loadThis.source = source;
    loadThis.result = makeValue();
    loadThis.symbol = lookupLocal("this");
    append(std::move(loadThis));

    Instruction instruction;
    instruction.kind = InstructionKind::Return;
    instruction.source = source;
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
    const std::unordered_map<const Expr *, int> *resolvedLocals,
    const std::unordered_map<const Function *, std::vector<std::string>>
        *functionUpvalues,
    std::string symbolPrefix)
    : resolvedLocals_(resolvedLocals), functionUpvalues_(functionUpvalues),
      symbolPrefix_(std::move(symbolPrefix)) {}

LoxModule
AstLowerer::lower(const std::string &moduleName,
                  const std::vector<std::unique_ptr<Stmt>> &statements,
                  const std::string &entryName) {
  LoxModule module(moduleName);
  LoxFunction &main = module.addFunction(entryName);
  uint32_t functionCounter = 0;
  FunctionLowerer lowerer(module, main, resolvedLocals_, functionUpvalues_, 0,
                          functionCounter, symbolPrefix_);
  lowerer.lower(statements);
  return module;
}

} // namespace eloxir::loxir
