#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <array>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

namespace cpplox {

struct Parser {
  Token current;
  Token previous;
  bool hadError = false;
  bool panicMode = false;
};

enum Precedence {
  PREC_NONE,
  PREC_ASSIGNMENT,
  PREC_OR,
  PREC_AND,
  PREC_EQUALITY,
  PREC_COMPARISON,
  PREC_TERM,
  PREC_FACTOR,
  PREC_UNARY,
  PREC_CALL,
  PREC_PRIMARY
};

struct Local {
  Token name;
  int depth = 0;
  bool isCaptured = false;
};
struct Upvalue {
  uint8_t index = 0;
  bool isLocal = false;
};
enum class FunctionType : uint8_t {
  Function,
  Initializer,
  Method,
  Script
};

inline constexpr FunctionType TYPE_FUNCTION = FunctionType::Function;
inline constexpr FunctionType TYPE_INITIALIZER = FunctionType::Initializer;
inline constexpr FunctionType TYPE_METHOD = FunctionType::Method;
inline constexpr FunctionType TYPE_SCRIPT = FunctionType::Script;

struct FunctionCompiler {
  FunctionCompiler *enclosing;
  ObjFunction *function;
  FunctionType type;

  std::array<Local, kUint8Count> locals;
  int localCount;
  std::array<Upvalue, kUint8Count> upvalues;
  int scopeDepth;
  int logicalByteCount;
};

struct LoopStart {
  int code;
  int logical;
};

struct ClassCompiler {
  ClassCompiler *enclosing;
  bool hasSuperclass;
};

class Compiler;
using ParseFn = void (Compiler::*)(bool canAssign);

struct ParseRule {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
};

static int chunkSize(Chunk *chunk) {
  return chunk->size();
}

class Compiler {
public:
  Compiler(Vm &vm, const char *source);
  ObjFunction *compile();

private:
  Chunk *currentChunk();
  LoopStart currentLoopStart();
  void errorAt(Token *token, const char *message);
  void error(const char *message);
  void errorAtCurrent(const char *message);
  void advance();
  void consume(TokenType type, const char *message);
  bool check(TokenType type);
  bool match(TokenType type);
  void emitByte(uint8_t byte);
  void emitBytes(uint8_t byte1, uint8_t byte2);
  void emitLoop(LoopStart loopStart);
  int emitJump(uint8_t instruction);
  void emitReturn();
  uint8_t makeConstant(Value value);
  void emitConstant(Value value);
  void patchJump(int offset);
  void initCompiler(FunctionCompiler *compiler, FunctionType type);
  ObjFunction *endCompiler();
  void beginScope();
  void endScope();
  void expression();
  void statement();
  void declaration();
  const ParseRule *getRule(TokenType type);
  void parsePrecedence(Precedence precedence);
  bool discardPureExpression(int start);
  void rememberGlobal(uint8_t constant);
  uint8_t identifierConstant(Token *name);
  static bool identifiersEqual(Token *a, Token *b);
  int resolveLocal(FunctionCompiler *compiler, Token *name);
  int addUpvalue(FunctionCompiler *compiler, uint8_t index, bool isLocal);
  int resolveUpvalue(FunctionCompiler *compiler, Token *name);
  void addLocal(Token name);
  void declareVariable();
  uint8_t parseVariable(const char *errorMessage);
  void markInitialized();
  void defineVariable(uint8_t global);
  uint8_t argumentList();
  void and_(bool canAssign);
  void binary(bool canAssign);
  void call(bool canAssign);
  void dot(bool canAssign);
  void literal(bool canAssign);
  void grouping(bool canAssign);
  void number(bool canAssign);
  void or_(bool canAssign);
  void string(bool canAssign);
  void namedVariable(Token name, bool canAssign);
  void variable(bool canAssign);
  static Token syntheticToken(const char *text);
  void super_(bool canAssign);
  void this_(bool canAssign);
  void unary(bool canAssign);
  void block();
  void function(FunctionType type);
  void method();
  void classDeclaration();
  void funDeclaration();
  void varDeclaration();
  void expressionStatement();
  void forStatement();
  void ifStatement();
  void printStatement();
  void returnStatement();
  void whileStatement();
  void synchronize();

  static const ParseRule rules[];

  Vm &vm;
  Parser parser;
  Scanner scanner;
  FunctionCompiler *current = nullptr;
  ClassCompiler *currentClass = nullptr;
  std::array<ObjString *, kUint8Count> knownGlobals{};
  int knownGlobalCount = 0;
};

Compiler::Compiler(Vm &vm, const char *source) : vm(vm) {
  scanner.reset(source);
}

Chunk *Compiler::currentChunk() { return &current->function->chunk; }

LoopStart Compiler::currentLoopStart() {
  LoopStart start;
  start.code = chunkSize(currentChunk());
  start.logical = current->logicalByteCount;
  return start;
}

void Compiler::errorAt(Token *token, const char *message) {
  if (parser.panicMode)
    return;
  parser.panicMode = true;
  std::fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    std::fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {

  } else {
    std::fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  std::fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}
void Compiler::error(const char *message) { errorAt(&parser.previous, message); }
void Compiler::errorAtCurrent(const char *message) {
  errorAt(&parser.current, message);
}

void Compiler::advance() {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanner.scanToken();
    if (parser.current.type != TOKEN_ERROR)
      break;

    errorAtCurrent(parser.current.start);
  }
}
void Compiler::consume(TokenType type, const char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}
bool Compiler::check(TokenType type) { return parser.current.type == type; }
bool Compiler::match(TokenType type) {
  if (!check(type))
    return false;
  advance();
  return true;
}
void Compiler::emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
  current->logicalByteCount++;
}
void Compiler::emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}
void Compiler::emitLoop(LoopStart loopStart) {
  emitByte(OP_LOOP);

  int logicalOffset = current->logicalByteCount - loopStart.logical + 2;
  if (logicalOffset > UINT16_MAX)
    error("Loop body too large.");

  int offset = chunkSize(currentChunk()) - loopStart.code + 2;
  if (offset > UINT16_MAX)
    error("Loop body too large.");

  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}
int Compiler::emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);
  return chunkSize(currentChunk()) - 2;
}
void Compiler::emitReturn() {

  if (current->type == TYPE_INITIALIZER) {
    emitBytes(OP_GET_LOCAL, 0);
  } else {
    emitByte(OP_NIL);
  }

  emitByte(OP_RETURN);
}
uint8_t Compiler::makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}
void Compiler::emitConstant(Value value) {
  uint8_t constant = makeConstant(value);
  if (constant <= 7) {
    emitByte((uint8_t)(OP_CONSTANT_0 + constant));
  } else {
    emitBytes(OP_CONSTANT, constant);
  }
}
void Compiler::patchJump(int offset) {

  int jump = chunkSize(currentChunk()) - offset - 2;

  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }

  currentChunk()->byteAt(offset) = (jump >> 8) & 0xff;
  currentChunk()->byteAt(offset + 1) = jump & 0xff;
}

void Compiler::initCompiler(FunctionCompiler *compiler, FunctionType type) {
  compiler->enclosing = current;
  compiler->function = nullptr;
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->logicalByteCount = 0;
  current = compiler;
  compiler->function = vm.newFunction();
  vm.addCompilerRoot(compiler->function);
  if (type != TYPE_SCRIPT) {
    current->function->name =
        vm.copyString(parser.previous.start, parser.previous.length);
  }

  Local *local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;

  if (type != TYPE_FUNCTION) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}

ObjFunction *Compiler::endCompiler() {
  emitReturn();
  ObjFunction *function = current->function;

#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {

    disassembleChunk(currentChunk(), function->name != nullptr
                                         ? function->name->chars
                                         : "<script>");
  }
#endif

  current = current->enclosing;
  vm.popCompilerRoot();
  return function;
}
void Compiler::beginScope() { current->scopeDepth++; }
void Compiler::endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {

    if (current->locals[current->localCount - 1].isCaptured) {
      emitByte(OP_CLOSE_UPVALUE);
    } else {
      emitByte(OP_POP);
    }
    current->localCount--;
  }
}

bool Compiler::discardPureExpression(int start) {
  Chunk *chunk = currentChunk();
  int depth = 0;
  for (int offset = start; offset < chunkSize(chunk);) {
    switch (chunk->byteAt(offset)) {
    case OP_CONSTANT:
      depth++;
      offset += 2;
      break;
    case OP_CONSTANT_0:
    case OP_CONSTANT_1:
    case OP_CONSTANT_2:
    case OP_CONSTANT_3:
    case OP_CONSTANT_4:
    case OP_CONSTANT_5:
    case OP_CONSTANT_6:
    case OP_CONSTANT_7:
      depth++;
      offset++;
      break;
    case OP_NIL:
    case OP_TRUE:
    case OP_FALSE:
      depth++;
      offset++;
      break;
    case OP_GET_LOCAL:
    case OP_GET_UPVALUE:
      depth++;
      offset += 2;
      break;
    case OP_GET_LOCAL_0:
    case OP_GET_LOCAL_1:
    case OP_GET_LOCAL_2:
    case OP_GET_LOCAL_3:
    case OP_GET_LOCAL_4:
    case OP_GET_LOCAL_5:
    case OP_GET_LOCAL_6:
    case OP_GET_LOCAL_7:
      depth++;
      offset++;
      break;
    case OP_GET_GLOBAL: {
      ObjString *name =
          asString(chunk->constantAt(chunk->byteAt(offset + 1)));
      bool known = name->length == 5 && std::memcmp(name->chars, "clock", 5) == 0;
      for (int i = 0; !known && i < knownGlobalCount; i++) {
        known = knownGlobals[i] == name;
      }
      if (!known)
        return false;
      depth++;
      offset += 2;
      break;
    }
    case OP_EQUAL:
      if (depth < 2)
        return false;
      depth--;
      offset++;
      break;
    case OP_NOT:
      if (depth < 1)
        return false;
      offset++;
      break;
    default:
      return false;
    }
  }
  if (depth == 1) {
    chunk->truncate(start);
    return true;
  }
  return false;
}

void Compiler::rememberGlobal(uint8_t constant) {
  ObjString *name = asString(currentChunk()->constantAt(constant));
  for (int i = 0; i < knownGlobalCount; i++) {
    if (knownGlobals[i] == name)
      return;
  }
  if (knownGlobalCount < kUint8Count)
    knownGlobals[knownGlobalCount++] = name;
}

uint8_t Compiler::identifierConstant(Token *name) {
  return makeConstant(
      objectValue(vm.copyString(name->start, name->length)));
}
bool Compiler::identifiersEqual(Token *a, Token *b) {
  if (a->length != b->length)
    return false;
  return std::memcmp(a->start, b->start, a->length) == 0;
}
int Compiler::resolveLocal(FunctionCompiler *compiler, Token *name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local *local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}
int Compiler::addUpvalue(FunctionCompiler *compiler, uint8_t index, bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue *upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == kUint8Count) {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}
int Compiler::resolveUpvalue(FunctionCompiler *compiler, Token *name) {
  if (compiler->enclosing == nullptr)
    return -1;

  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(compiler, (uint8_t)upvalue, false);
  }

  return -1;
}
void Compiler::addLocal(Token name) {
  if (current->localCount == kUint8Count) {
    error("Too many local variables in function.");
    return;
  }

  Local *local = &current->locals[current->localCount++];
  local->name = name;

  local->depth = -1;
  local->isCaptured = false;
}
void Compiler::declareVariable() {
  if (current->scopeDepth == 0)
    return;

  Token *name = &parser.previous;
  for (int i = current->localCount - 1; i >= 0; i--) {
    Local *local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Already a variable with this name in this scope.");
    }
  }

  addLocal(*name);
}
uint8_t Compiler::parseVariable(const char *errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable();
  if (current->scopeDepth > 0)
    return 0;

  return identifierConstant(&parser.previous);
}
void Compiler::markInitialized() {
  if (current->scopeDepth == 0)
    return;
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}
void Compiler::defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
  rememberGlobal(global);
}
uint8_t Compiler::argumentList() {
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      if (argCount == 255) {
        error("Can't have more than 255 arguments.");
      }
      argCount++;
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}
void Compiler::and_(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(endJump);
}

void Compiler::binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  const ParseRule *rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
  case TOKEN_BANG_EQUAL:
    emitBytes(OP_EQUAL, OP_NOT);
    break;
  case TOKEN_EQUAL_EQUAL:
    emitByte(OP_EQUAL);
    break;
  case TOKEN_GREATER:
    emitByte(OP_GREATER);
    break;
  case TOKEN_GREATER_EQUAL:
    emitBytes(OP_LESS, OP_NOT);
    break;
  case TOKEN_LESS:
    emitByte(OP_LESS);
    break;
  case TOKEN_LESS_EQUAL:
    emitBytes(OP_GREATER, OP_NOT);
    break;
  case TOKEN_PLUS:
    emitByte(OP_ADD);
    break;
  case TOKEN_MINUS:
    emitByte(OP_SUBTRACT);
    break;
  case TOKEN_STAR:
    emitByte(OP_MULTIPLY);
    break;
  case TOKEN_SLASH:
    emitByte(OP_DIVIDE);
    break;
  default:
    return;
  }
}
void Compiler::call(bool canAssign) {
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
}
void Compiler::dot(bool canAssign) {
  consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
  uint8_t name = identifierConstant(&parser.previous);

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(OP_SET_PROPERTY, name);
  } else if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    emitBytes(OP_INVOKE, name);
    emitByte(argCount);
  } else {
    emitBytes(OP_GET_PROPERTY, name);
  }
}

void Compiler::literal(bool canAssign) {
  switch (parser.previous.type) {
  case TOKEN_FALSE:
    emitByte(OP_FALSE);
    break;
  case TOKEN_NIL:
    emitByte(OP_NIL);
    break;
  case TOKEN_TRUE:
    emitByte(OP_TRUE);
    break;
  default:
    return;
  }
}

void Compiler::grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

void Compiler::number(bool canAssign) {
  double value = std::strtod(parser.previous.start, nullptr);

  emitConstant(numberValue(value));
}
void Compiler::or_(bool canAssign) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

void Compiler::string(bool canAssign) {
  emitConstant(objectValue(vm.copyString(
      parser.previous.start + 1, parser.previous.length - 2)));
}

void Compiler::namedVariable(Token name, bool canAssign) {

  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();

    if (setOp == OP_SET_LOCAL && arg >= 0 && arg <= 7) {
      emitByte((uint8_t)(OP_SET_LOCAL_0 + arg));
    } else {
      emitBytes(setOp, (uint8_t)arg);
    }
  } else {

    if (getOp == OP_GET_LOCAL && arg >= 0 && arg <= 7) {
      emitByte((uint8_t)(OP_GET_LOCAL_0 + arg));
    } else {
      emitBytes(getOp, (uint8_t)arg);
    }
  }
}

void Compiler::variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}
Token Compiler::syntheticToken(const char *text) {
  Token token;
  token.start = text;
  token.length = (int)std::strlen(text);
  return token;
}
void Compiler::super_(bool canAssign) {
  if (currentClass == nullptr) {
    error("Can't use 'super' outside of a class.");
  } else if (!currentClass->hasSuperclass) {
    error("Can't use 'super' in a class with no superclass.");
  }

  consume(TOKEN_DOT, "Expect '.' after 'super'.");
  consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
  uint8_t name = identifierConstant(&parser.previous);

  namedVariable(syntheticToken("this"), false);

  if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    namedVariable(syntheticToken("super"), false);
    emitBytes(OP_SUPER_INVOKE, name);
    emitByte(argCount);
  } else {
    namedVariable(syntheticToken("super"), false);
    emitBytes(OP_GET_SUPER, name);
  }
}
void Compiler::this_(bool canAssign) {
  if (currentClass == nullptr) {
    error("Can't use 'this' outside of a class.");
    return;
  }

  variable(false);
}

void Compiler::unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  parsePrecedence(PREC_UNARY);

  switch (operatorType) {
  case TOKEN_BANG:
    emitByte(OP_NOT);
    break;
  case TOKEN_MINUS:
    emitByte(OP_NEGATE);
    break;
  default:
    return;
  }
}
const ParseRule Compiler::rules[] = {

    {&Compiler::grouping, &Compiler::call, PREC_CALL},
    {nullptr, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},

    {nullptr, &Compiler::dot, PREC_CALL},
    {&Compiler::unary, &Compiler::binary, PREC_TERM},
    {nullptr, &Compiler::binary, PREC_TERM},
    {nullptr, nullptr, PREC_NONE},
    {nullptr, &Compiler::binary, PREC_FACTOR},
    {nullptr, &Compiler::binary, PREC_FACTOR},

    {&Compiler::unary, nullptr, PREC_NONE},

    {nullptr, &Compiler::binary, PREC_EQUALITY},
    {nullptr, nullptr, PREC_NONE},

    {nullptr, &Compiler::binary, PREC_EQUALITY},
    {nullptr, &Compiler::binary, PREC_COMPARISON},
    {nullptr, &Compiler::binary, PREC_COMPARISON},
    {nullptr, &Compiler::binary, PREC_COMPARISON},
    {nullptr, &Compiler::binary, PREC_COMPARISON},

    {&Compiler::variable, nullptr, PREC_NONE},

    {&Compiler::string, nullptr, PREC_NONE},
    {&Compiler::number, nullptr, PREC_NONE},

    {nullptr, &Compiler::and_, PREC_AND},
    {nullptr, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},

    {&Compiler::literal, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},

    {&Compiler::literal, nullptr, PREC_NONE},

    {nullptr, &Compiler::or_, PREC_OR},
    {nullptr, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},

    {&Compiler::super_, nullptr, PREC_NONE},

    {&Compiler::this_, nullptr, PREC_NONE},

    {&Compiler::literal, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},
    {nullptr, nullptr, PREC_NONE},
};
void Compiler::parsePrecedence(Precedence precedence) {

  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == nullptr) {
    error("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  (this->*prefixRule)(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;

    (this->*infixRule)(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}
const ParseRule *Compiler::getRule(TokenType type) { return &rules[tokenIndex(type)]; }
void Compiler::expression() { parsePrecedence(PREC_ASSIGNMENT); }
void Compiler::block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}
void Compiler::function(FunctionType type) {
  FunctionCompiler compiler;
  initCompiler(&compiler, type);
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;
      if (current->function->arity > 255) {
        errorAtCurrent("Can't have more than 255 parameters.");
      }
      uint8_t constant = parseVariable("Expect parameter name.");
      defineVariable(constant);
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
  consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block();

  ObjFunction *function = endCompiler();

  emitBytes(OP_CLOSURE, makeConstant(objectValue(function)));

  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}
void Compiler::method() {
  consume(TOKEN_IDENTIFIER, "Expect method name.");
  uint8_t constant = identifierConstant(&parser.previous);

  FunctionType type = TYPE_METHOD;
  if (parser.previous.length == 4 &&
      std::memcmp(parser.previous.start, "init", 4) == 0) {
    type = TYPE_INITIALIZER;
  }

  function(type);
  emitBytes(OP_METHOD, constant);
}
void Compiler::classDeclaration() {
  consume(TOKEN_IDENTIFIER, "Expect class name.");
  Token className = parser.previous;
  uint8_t nameConstant = identifierConstant(&parser.previous);
  declareVariable();

  emitBytes(OP_CLASS, nameConstant);
  defineVariable(nameConstant);

  ClassCompiler classCompiler;
  classCompiler.hasSuperclass = false;
  classCompiler.enclosing = currentClass;
  currentClass = &classCompiler;

  if (match(TOKEN_LESS)) {
    consume(TOKEN_IDENTIFIER, "Expect superclass name.");
    variable(false);

    if (identifiersEqual(&className, &parser.previous)) {
      error("A class can't inherit from itself.");
    }

    beginScope();
    addLocal(syntheticToken("super"));
    defineVariable(0);

    namedVariable(className, false);
    emitByte(OP_INHERIT);
    classCompiler.hasSuperclass = true;
  }

  namedVariable(className, false);
  consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    method();
  }
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
  emitByte(OP_POP);

  if (classCompiler.hasSuperclass) {
    endScope();
  }

  currentClass = currentClass->enclosing;
}
void Compiler::funDeclaration() {
  uint8_t global = parseVariable("Expect function name.");
  markInitialized();
  function(TYPE_FUNCTION);
  defineVariable(global);
}
void Compiler::varDeclaration() {
  uint8_t global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(OP_NIL);
  }
  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

  defineVariable(global);
}
void Compiler::expressionStatement() {
  int start = chunkSize(currentChunk());
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  if (discardPureExpression(start)) {
    current->logicalByteCount++;
    return;
  }
  emitByte(OP_POP);
}
void Compiler::forStatement() {
  beginScope();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

  if (match(TOKEN_SEMICOLON)) {

  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    expressionStatement();
  }

  LoopStart loopStart = currentLoopStart();

  int exitJump = -1;
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
  }

  if (!match(TOKEN_RIGHT_PAREN)) {
    int bodyJump = emitJump(OP_JUMP);
    LoopStart incrementStart = currentLoopStart();
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJump);
  }

  statement();
  emitLoop(loopStart);

  if (exitJump != -1) {
    patchJump(exitJump);
    emitByte(OP_POP);
  }

  endScope();
}
void Compiler::ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE))
    statement();
  patchJump(elseJump);
}
void Compiler::printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}
void Compiler::returnStatement() {
  if (current->type == TYPE_SCRIPT) {
    error("Can't return from top-level code.");
  }

  if (match(TOKEN_SEMICOLON)) {
    emitReturn();
  } else {
    if (current->type == TYPE_INITIALIZER) {
      error("Can't return a value from an initializer.");
    }

    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emitByte(OP_RETURN);
  }
}
void Compiler::whileStatement() {
  LoopStart loopStart = currentLoopStart();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();
  emitLoop(loopStart);

  patchJump(exitJump);
  emitByte(OP_POP);
}
void Compiler::synchronize() {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON)
      return;
    switch (parser.current.type) {
    case TOKEN_CLASS:
    case TOKEN_FUN:
    case TOKEN_VAR:
    case TOKEN_FOR:
    case TOKEN_IF:
    case TOKEN_WHILE:
    case TOKEN_PRINT:
    case TOKEN_RETURN:
      return;

    default:;
    }

    advance();
  }
}
void Compiler::declaration() {
  if (match(TOKEN_CLASS)) {
    classDeclaration();

  } else if (match(TOKEN_FUN)) {
    funDeclaration();

  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode)
    synchronize();
}
void Compiler::statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

ObjFunction *Compiler::compile() {
  knownGlobalCount = 0;

  FunctionCompiler functionCompiler;

  initCompiler(&functionCompiler, TYPE_SCRIPT);

  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  ObjFunction *function = endCompiler();
  return parser.hadError ? nullptr : function;
}

ObjFunction *compile(Vm &vm, const char *source) {
  Compiler compiler(vm, source);
  return compiler.compile();
}

} // namespace cpplox
