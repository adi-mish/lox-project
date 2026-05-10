#include <cstdio>
#include <cstring>
#include <iostream>
#include <string_view>
#include <ctime>
#ifdef CPPLOX_ENABLE_VM_STATS
#include <cinttypes>
#endif

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

namespace cpplox {

#ifdef CPPLOX_ENABLE_VM_STATS
static const char *opcodeName(int opcode) {
  switch (opcode) {
  case OP_CONSTANT:
    return "OP_CONSTANT";
  case OP_CONSTANT_0:
    return "OP_CONSTANT_0";
  case OP_CONSTANT_1:
    return "OP_CONSTANT_1";
  case OP_CONSTANT_2:
    return "OP_CONSTANT_2";
  case OP_CONSTANT_3:
    return "OP_CONSTANT_3";
  case OP_CONSTANT_4:
    return "OP_CONSTANT_4";
  case OP_CONSTANT_5:
    return "OP_CONSTANT_5";
  case OP_CONSTANT_6:
    return "OP_CONSTANT_6";
  case OP_CONSTANT_7:
    return "OP_CONSTANT_7";
  case OP_NIL:
    return "OP_NIL";
  case OP_TRUE:
    return "OP_TRUE";
  case OP_FALSE:
    return "OP_FALSE";
  case OP_POP:
    return "OP_POP";
  case OP_GET_LOCAL:
    return "OP_GET_LOCAL";
  case OP_GET_LOCAL_0:
    return "OP_GET_LOCAL_0";
  case OP_GET_LOCAL_1:
    return "OP_GET_LOCAL_1";
  case OP_GET_LOCAL_2:
    return "OP_GET_LOCAL_2";
  case OP_GET_LOCAL_3:
    return "OP_GET_LOCAL_3";
  case OP_GET_LOCAL_4:
    return "OP_GET_LOCAL_4";
  case OP_GET_LOCAL_5:
    return "OP_GET_LOCAL_5";
  case OP_GET_LOCAL_6:
    return "OP_GET_LOCAL_6";
  case OP_GET_LOCAL_7:
    return "OP_GET_LOCAL_7";
  case OP_SET_LOCAL:
    return "OP_SET_LOCAL";
  case OP_SET_LOCAL_0:
    return "OP_SET_LOCAL_0";
  case OP_SET_LOCAL_1:
    return "OP_SET_LOCAL_1";
  case OP_SET_LOCAL_2:
    return "OP_SET_LOCAL_2";
  case OP_SET_LOCAL_3:
    return "OP_SET_LOCAL_3";
  case OP_SET_LOCAL_4:
    return "OP_SET_LOCAL_4";
  case OP_SET_LOCAL_5:
    return "OP_SET_LOCAL_5";
  case OP_SET_LOCAL_6:
    return "OP_SET_LOCAL_6";
  case OP_SET_LOCAL_7:
    return "OP_SET_LOCAL_7";
  case OP_GET_GLOBAL:
    return "OP_GET_GLOBAL";
  case OP_DEFINE_GLOBAL:
    return "OP_DEFINE_GLOBAL";
  case OP_SET_GLOBAL:
    return "OP_SET_GLOBAL";
  case OP_GET_UPVALUE:
    return "OP_GET_UPVALUE";
  case OP_SET_UPVALUE:
    return "OP_SET_UPVALUE";
  case OP_GET_PROPERTY:
    return "OP_GET_PROPERTY";
  case OP_SET_PROPERTY:
    return "OP_SET_PROPERTY";
  case OP_GET_SUPER:
    return "OP_GET_SUPER";
  case OP_EQUAL:
    return "OP_EQUAL";
  case OP_GREATER:
    return "OP_GREATER";
  case OP_LESS:
    return "OP_LESS";
  case OP_ADD:
    return "OP_ADD";
  case OP_SUBTRACT:
    return "OP_SUBTRACT";
  case OP_MULTIPLY:
    return "OP_MULTIPLY";
  case OP_DIVIDE:
    return "OP_DIVIDE";
  case OP_NOT:
    return "OP_NOT";
  case OP_NEGATE:
    return "OP_NEGATE";
  case OP_PRINT:
    return "OP_PRINT";
  case OP_JUMP:
    return "OP_JUMP";
  case OP_JUMP_IF_FALSE:
    return "OP_JUMP_IF_FALSE";
  case OP_LOOP:
    return "OP_LOOP";
  case OP_CALL:
    return "OP_CALL";
  case OP_INVOKE:
    return "OP_INVOKE";
  case OP_SUPER_INVOKE:
    return "OP_SUPER_INVOKE";
  case OP_CLOSURE:
    return "OP_CLOSURE";
  case OP_CLOSE_UPVALUE:
    return "OP_CLOSE_UPVALUE";
  case OP_RETURN:
    return "OP_RETURN";
  case OP_CLASS:
    return "OP_CLASS";
  case OP_INHERIT:
    return "OP_INHERIT";
  case OP_METHOD:
    return "OP_METHOD";
  }
  return "OP_UNKNOWN";
}

void Vm::setStatsEnabled(bool enabled) { statsEnabled = enabled; }

void Vm::resetStats() {
  bool enabled = statsEnabled;
  opcodeCounts.fill(0);
  instructionsExecuted = 0;
  maxStackDepth = 0;
  closureCalls = 0;
  nativeCalls = 0;
  classCalls = 0;
  boundMethodCalls = 0;
  invokes = 0;
  globalCacheHits = 0;
  globalCacheMisses = 0;
  methodCacheHits = 0;
  methodCacheMisses = 0;
  fieldCacheHits = 0;
  fieldCacheMisses = 0;
  statsEnabled = enabled;
}

static void recordInstruction(Vm &vm, uint8_t opcode) {
  if (!vm.statsEnabled)
    return;
  vm.instructionsExecuted++;
  vm.opcodeCounts[opcode]++;
  uint64_t depth = (uint64_t)(vm.stackTop - vm.stack.data());
  if (depth > vm.maxStackDepth)
    vm.maxStackDepth = depth;
}

void Vm::printStats() const {
  std::fprintf(stderr, "cpplox VM stats:\n");
  std::fprintf(stderr, "  instructions: %" PRIu64 "\n", instructionsExecuted);
  std::fprintf(stderr, "  max_stack_depth: %" PRIu64 "\n", maxStackDepth);
  std::fprintf(stderr, "  bytes_allocated: %zu\n", heap.bytesAllocated());
  std::fprintf(stderr, "  closure_calls: %" PRIu64 "\n", closureCalls);
  std::fprintf(stderr, "  native_calls: %" PRIu64 "\n", nativeCalls);
  std::fprintf(stderr, "  class_calls: %" PRIu64 "\n", classCalls);
  std::fprintf(stderr, "  bound_method_calls: %" PRIu64 "\n", boundMethodCalls);
  std::fprintf(stderr, "  invokes: %" PRIu64 "\n", invokes);
  std::fprintf(stderr, "  global_cache_hits: %" PRIu64 "\n", globalCacheHits);
  std::fprintf(stderr, "  global_cache_misses: %" PRIu64 "\n", globalCacheMisses);
  std::fprintf(stderr, "  method_cache_hits: %" PRIu64 "\n", methodCacheHits);
  std::fprintf(stderr, "  method_cache_misses: %" PRIu64 "\n", methodCacheMisses);
  std::fprintf(stderr, "  field_cache_hits: %" PRIu64 "\n", fieldCacheHits);
  std::fprintf(stderr, "  field_cache_misses: %" PRIu64 "\n", fieldCacheMisses);
  std::fprintf(stderr, "  opcodes:\n");
  for (int i = 0; i < OP_COUNT; i++) {
    if (opcodeCounts[i] == 0)
      continue;
    std::fprintf(stderr, "    %-20s %" PRIu64 "\n", opcodeName(i),
            opcodeCounts[i]);
  }
}
#else
static void recordInstruction(Vm &, uint8_t) {}
#endif

#ifdef CPPLOX_ENABLE_VM_STATS
static void recordGlobalCacheHit(Vm &vm) {
  if (vm.statsEnabled)
    vm.globalCacheHits++;
}
static void recordGlobalCacheMiss(Vm &vm) {
  if (vm.statsEnabled)
    vm.globalCacheMisses++;
}
static void recordMethodCacheHit(Vm &vm) {
  if (vm.statsEnabled)
    vm.methodCacheHits++;
}
static void recordMethodCacheMiss(Vm &vm) {
  if (vm.statsEnabled)
    vm.methodCacheMisses++;
}
static void recordFieldCacheHit(Vm &vm) {
  if (vm.statsEnabled)
    vm.fieldCacheHits++;
}
static void recordFieldCacheMiss(Vm &vm) {
  if (vm.statsEnabled)
    vm.fieldCacheMisses++;
}
#else
static void recordGlobalCacheHit(Vm &) {}
static void recordGlobalCacheMiss(Vm &) {}
static void recordMethodCacheHit(Vm &) {}
static void recordMethodCacheMiss(Vm &) {}
static void recordFieldCacheHit(Vm &) {}
static void recordFieldCacheMiss(Vm &) {}
#endif

static Value clockNative(int argCount, Value *args) {
  return numberValue((double)std::clock() / CLOCKS_PER_SEC);
}

Vm::Vm() { initialize(); }

Vm::~Vm() { shutdown(); }

static void resetStack(Vm &vm) {
  vm.stackTop = vm.stack.data();
  vm.frameCount = 0;
  vm.openUpvalues = nullptr;
}
template <typename... Parts> static void runtimeError(Vm &vm, Parts &&...parts) {
  (std::cerr << ... << parts) << '\n';

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame *frame = &vm.frames[i];

    ObjFunction *function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.codeData() - 1;
    std::cerr << "[line " << function->chunk.lineAt(instruction) << "] in ";
    if (function->name == nullptr) {
      std::cerr << "script\n";
    } else {
      std::cerr << function->name->chars << "()\n";
    }
  }

  resetStack(vm);
}
static void defineNative(Vm &vm, const char *name, NativeFn function) {
  vm.push(objectValue(vm.copyString(name, (int)std::strlen(name))));
  vm.push(objectValue(vm.newNative(function)));
  vm.globals.set(asString(vm.stack[0]), vm.stack[1]);
  vm.pop();
  vm.pop();
}

void Vm::initialize() {
  Vm &vm = *this;
  resetStack(vm);
#ifdef CPPLOX_ENABLE_VM_STATS
  vm.statsEnabled = false;
  resetStats();
#endif
  vm.heap.initialize();

  vm.globals.clear();
  vm.strings.clear();

  vm.initString = nullptr;
  vm.initString = vm.copyString("init", 4);

  defineNative(vm, "clock", clockNative);
}

void Vm::shutdown() {
  Vm &vm = *this;
  vm.globals.clear();
  vm.strings.clear();
  vm.initString = nullptr;
  freeObjects(*this);
}
void Vm::push(Value value) {
  *stackTop = value;
  stackTop++;
}
Value Vm::pop() {
  stackTop--;
  return *stackTop;
}

void Vm::addCompilerRoot(ObjFunction *function) {
  compilerRoots.push_back(function);
}

void Vm::popCompilerRoot() { compilerRoots.pop_back(); }

void Vm::markCompilerRoots() {
  for (ObjFunction *function : compilerRoots) {
    markObject(*this, function);
  }
}

static Value peek(Vm &vm, int distance) {
  return vm.stackTop[-1 - distance];
}

static bool call(Vm &vm, ObjClosure *closure, int argCount) {
  if (argCount != closure->function->arity) {
    runtimeError(vm, "Expected ", closure->function->arity,
                 " arguments but got ", argCount, ".");
    return false;
  }

  if (vm.frameCount == kMaxFrames) {
    runtimeError(vm, "Stack overflow.");
    return false;
  }

  CallFrame *frame = &vm.frames[vm.frameCount++];

  frame->closure = closure;
  frame->ip = closure->function->chunk.codeData();
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}
static bool callValue(Vm &vm, Value callee, int argCount) {
  if (isObj(callee)) {
    switch (objectType(callee)) {
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod *bound = asBoundMethod(callee);
#ifdef CPPLOX_ENABLE_VM_STATS
      if (vm.statsEnabled)
        vm.boundMethodCalls++;
#endif
      vm.stackTop[-argCount - 1] = bound->receiver;
      return call(vm, bound->method, argCount);
    }
    case OBJ_CLASS: {
      ObjClass *klass = asClass(callee);
#ifdef CPPLOX_ENABLE_VM_STATS
      if (vm.statsEnabled)
        vm.classCalls++;
#endif
      vm.stackTop[-argCount - 1] = objectValue(vm.newInstance(klass));
      if (klass->initializer != nullptr) {
        return call(vm, klass->initializer, argCount);
      } else if (argCount != 0) {
        runtimeError(vm, "Expected 0 arguments but got ", argCount, ".");
        return false;
      }
      return true;
    }
    case OBJ_CLOSURE:
#ifdef CPPLOX_ENABLE_VM_STATS
      if (vm.statsEnabled)
        vm.closureCalls++;
#endif
      return call(vm, asClosure(callee), argCount);

    case OBJ_NATIVE: {
      NativeFn native = asNative(callee);
#ifdef CPPLOX_ENABLE_VM_STATS
      if (vm.statsEnabled)
        vm.nativeCalls++;
#endif
      Value result = native(argCount, vm.stackTop - argCount);
      vm.stackTop -= argCount + 1;
      vm.push(result);
      return true;
    }
    default:
      break;
    }
  }
  runtimeError(vm, "Can only call functions and classes.");
  return false;
}

static bool getFieldSlot(ObjClass *klass, ObjString *name, int *slot) {
  Value slotValue;
  if (!klass->fieldSlots.get(name, &slotValue))
    return false;
  *slot = (int)asNumber(slotValue);
  return true;
}

static int ensureFieldSlot(ObjClass *klass, ObjString *name) {
  int slot;
  if (getFieldSlot(klass, name, &slot))
    return slot;

  slot = klass->fieldSlotCount++;
  klass->fieldSlots.set(name, numberValue(slot));
  klass->fieldVersion++;
  return slot;
}

static bool readInstanceField(ObjInstance *instance, int slot, Value *value) {
  return instance->fields.read(slot, value);
}

static void writeInstanceField(ObjInstance *instance, int slot, Value value) {
  instance->fields.write(slot, value);
}

static bool findMethodCached(Vm &vm, ObjClass *klass, ObjString *name,
                             InlineCache *cache, Value *method) {
  if (cache != nullptr && cache->kind == CACHE_METHOD && cache->key == name &&
      cache->ownerClass == klass &&
      cache->tableVersion == klass->methods.version()) {
    recordMethodCacheHit(vm);
    *method = cache->value;
    return true;
  }

  recordMethodCacheMiss(vm);
  if (!klass->methods.get(name, method)) {
    runtimeError(vm, "Undefined property '", name->chars, "'.");
    return false;
  }

  if (cache != nullptr) {
    cache->kind = CACHE_METHOD;
    cache->key = name;
    cache->ownerClass = klass;
    cache->value = *method;
    cache->tableVersion = klass->methods.version();
    cache->secondaryVersion = 0;
    cache->entryIndex = -2;
  }
  return true;
}

static bool invokeFromClass(Vm &vm, ObjClass *klass, ObjString *name,
                            int argCount, InlineCache *cache) {
  Value method;
  if (!findMethodCached(vm, klass, name, cache, &method))
    return false;
  return call(vm, asClosure(method), argCount);
}

static bool invoke(Vm &vm, ObjString *name, int argCount, InlineCache *cache) {
#ifdef CPPLOX_ENABLE_VM_STATS
  if (vm.statsEnabled)
    vm.invokes++;
#endif
  Value receiver = peek(vm, argCount);

  if (!isInstance(receiver)) {
    runtimeError(vm, "Only instances have methods.");
    return false;
  }

  ObjInstance *instance = asInstance(receiver);
  if (cache != nullptr && cache->kind == CACHE_METHOD && cache->key == name &&
      cache->ownerClass == instance->klass &&
      cache->tableVersion == instance->klass->methods.version() &&
      cache->secondaryVersion == instance->klass->fieldVersion) {
    if (cache->entryIndex == -1) {
      recordMethodCacheHit(vm);
      return call(vm, asClosure(cache->value), argCount);
    }
    if (cache->entryIndex >= 0) {
      Value ignored;
      if (!readInstanceField(instance, cache->entryIndex, &ignored)) {
        recordMethodCacheHit(vm);
        return call(vm, asClosure(cache->value), argCount);
      }
    }
  }

  Value value;
  int fieldSlot = -1;
  if (getFieldSlot(instance->klass, name, &fieldSlot) &&
      readInstanceField(instance, fieldSlot, &value)) {
    vm.stackTop[-argCount - 1] = value;
    return callValue(vm, value, argCount);
  }

  if (!findMethodCached(vm, instance->klass, name, cache, &value))
    return false;
  if (cache != nullptr) {
    cache->secondaryVersion = instance->klass->fieldVersion;
    cache->entryIndex = fieldSlot >= 0 ? fieldSlot : -1;
  }
  return call(vm, asClosure(value), argCount);
}
static bool bindMethodCached(Vm &vm, ObjClass *klass, ObjString *name,
                             InlineCache *cache) {
  Value method;
  if (!findMethodCached(vm, klass, name, cache, &method))
    return false;

  ObjBoundMethod *bound = vm.newBoundMethod(peek(vm, 0), asClosure(method));
  vm.pop();
  vm.push(objectValue(bound));
  return true;
}
static bool bindMethod(Vm &vm, ObjClass *klass, ObjString *name) {
  return bindMethodCached(vm, klass, name, nullptr);
}
static ObjUpvalue *captureUpvalue(Vm &vm, Value *local) {
  ObjUpvalue *prevUpvalue = nullptr;
  ObjUpvalue *upvalue = vm.openUpvalues;
  while (upvalue != nullptr && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != nullptr && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue *createdUpvalue = vm.newUpvalue(local);
  createdUpvalue->next = upvalue;

  if (prevUpvalue == nullptr) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}
static void closeUpvalues(Vm &vm, Value *last) {
  while (vm.openUpvalues != nullptr && vm.openUpvalues->location >= last) {
    ObjUpvalue *upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}
static void defineMethod(Vm &vm, ObjString *name) {
  Value method = peek(vm, 0);
  ObjClass *klass = asClass(peek(vm, 1));
  klass->methods.set(name, method);
  if (name == vm.initString) {
    klass->initializer = asClosure(method);
  }
  vm.pop();
}
static bool isFalsey(Value value) {
  return isNil(value) || (isBool(value) && !asBool(value));
}
static void concatenate(Vm &vm) {
  ObjString *b = asString(peek(vm, 0));
  ObjString *a = asString(peek(vm, 1));

  int length = a->length + b->length;
  char *chars = allocate<char>(vm, length + 1);
  std::memcpy(chars, a->chars, a->length);
  std::memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString *result = vm.takeString(chars, length);
  vm.pop();
  vm.pop();
  vm.push(objectValue(result));
}
static InterpretResult run(Vm &vm) {
  CallFrame *frame = &vm.frames[vm.frameCount - 1];

  auto readByte = [&]() -> uint8_t { return *frame->ip++; };
  auto readShort = [&]() -> uint16_t {
    frame->ip += 2;
    return static_cast<uint16_t>((frame->ip[-2] << 8) | frame->ip[-1]);
  };
  auto readConstant = [&]() -> Value {
    return frame->closure->function->chunk.constantAt(readByte());
  };
  auto readString = [&]() -> ObjString * { return asString(readConstant()); };
  auto pushValue = [&](Value value) { *vm.stackTop++ = value; };
  auto popValue = [&]() -> Value { return *--vm.stackTop; };
  auto binaryOp = [&](auto makeValue, auto op) -> bool {
    Value bValue = vm.stackTop[-1];
    Value aValue = vm.stackTop[-2];
    if (!isNumber(bValue) || !isNumber(aValue)) {
      runtimeError(vm, "Operands must be numbers.");
      return false;
    }
    vm.stackTop[-2] = makeValue(op(asNumber(aValue), asNumber(bValue)));
    vm.stackTop--;
    return true;
  };

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    std::printf("          ");
    for (Value *slot = vm.stack.data(); slot < vm.stackTop; slot++) {
      std::printf("[ ");
      printValue(*slot);
      std::printf(" ]");
    }
    std::printf("\n");

    disassembleInstruction(
        &frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.codeData()));
#endif

    uint8_t instruction = readByte();
    recordInstruction(vm, instruction);
    switch (instruction) {
    case OP_CONSTANT: {
      Value constant = readConstant();

      pushValue(constant);
      break;
    }
    case OP_CONSTANT_0:
    case OP_CONSTANT_1:
    case OP_CONSTANT_2:
    case OP_CONSTANT_3:
    case OP_CONSTANT_4:
    case OP_CONSTANT_5:
    case OP_CONSTANT_6:
    case OP_CONSTANT_7: {
      Value *constants = frame->closure->function->chunk.constantsData();
      pushValue(constants[instruction - OP_CONSTANT_0]);
      break;
    }
    case OP_NIL:
      pushValue(nilValue());
      break;
    case OP_TRUE:
      pushValue(boolValue(true));
      break;
    case OP_FALSE:
      pushValue(boolValue(false));
      break;
    case OP_POP:
      vm.stackTop--;
      break;
    case OP_GET_LOCAL: {
      uint8_t slot = readByte();

      pushValue(frame->slots[slot]);
      break;
    }
    case OP_GET_LOCAL_0:
      pushValue(frame->slots[0]);
      break;
    case OP_GET_LOCAL_1:
      pushValue(frame->slots[1]);
      break;
    case OP_GET_LOCAL_2:
      pushValue(frame->slots[2]);
      break;
    case OP_GET_LOCAL_3:
      pushValue(frame->slots[3]);
      break;
    case OP_GET_LOCAL_4:
      pushValue(frame->slots[4]);
      break;
    case OP_GET_LOCAL_5:
      pushValue(frame->slots[5]);
      break;
    case OP_GET_LOCAL_6:
      pushValue(frame->slots[6]);
      break;
    case OP_GET_LOCAL_7:
      pushValue(frame->slots[7]);
      break;
    case OP_SET_LOCAL: {
      uint8_t slot = readByte();

      frame->slots[slot] = peek(vm, 0);
      break;
    }
    case OP_SET_LOCAL_0:
      frame->slots[0] = vm.stackTop[-1];
      break;
    case OP_SET_LOCAL_1:
      frame->slots[1] = vm.stackTop[-1];
      break;
    case OP_SET_LOCAL_2:
      frame->slots[2] = vm.stackTop[-1];
      break;
    case OP_SET_LOCAL_3:
      frame->slots[3] = vm.stackTop[-1];
      break;
    case OP_SET_LOCAL_4:
      frame->slots[4] = vm.stackTop[-1];
      break;
    case OP_SET_LOCAL_5:
      frame->slots[5] = vm.stackTop[-1];
      break;
    case OP_SET_LOCAL_6:
      frame->slots[6] = vm.stackTop[-1];
      break;
    case OP_SET_LOCAL_7:
      frame->slots[7] = vm.stackTop[-1];
      break;
    case OP_GET_GLOBAL: {
      uint8_t constant = readByte();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *name = asString(chunk->constantAt(constant));
      InlineCache *cache = &chunk->inlineCache(constant);

      Entry *entry = cache->entry;
      if (cache->kind == CACHE_GLOBAL && cache->key == name &&
          cache->tableVersion == vm.globals.version() && entry != nullptr) {
        recordGlobalCacheHit(vm);
        pushValue(entry->value);
        break;
      }

      recordGlobalCacheMiss(vm);
      entry = vm.globals.getEntry(name);
      if (entry == nullptr) {
        runtimeError(vm, "Undefined variable '", name->chars, "'.");
        return INTERPRET_RUNTIME_ERROR;
      }

      cache->key = name;
      cache->entry = entry;
      cache->tableVersion = vm.globals.version();
      cache->kind = CACHE_GLOBAL;
      pushValue(entry->value);
      break;
    }
    case OP_DEFINE_GLOBAL: {
      uint8_t constant = readByte();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *name = asString(chunk->constantAt(constant));
      vm.globals.set(name, vm.stackTop[-1]);
      InlineCache *cache = &chunk->inlineCache(constant);
      cache->key = name;
      cache->entry = vm.globals.getEntry(name);
      cache->tableVersion = vm.globals.version();
      cache->kind = CACHE_GLOBAL;
      vm.stackTop--;
      break;
    }
    case OP_SET_GLOBAL: {
      uint8_t constant = readByte();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *name = asString(chunk->constantAt(constant));
      InlineCache *cache = &chunk->inlineCache(constant);

      Entry *entry = cache->entry;
      if (!(cache->kind == CACHE_GLOBAL && cache->key == name &&
            cache->tableVersion == vm.globals.version() && entry != nullptr)) {
        recordGlobalCacheMiss(vm);
        entry = vm.globals.getEntry(name);
        if (entry == nullptr) {
          runtimeError(vm, "Undefined variable '", name->chars, "'.");
          return INTERPRET_RUNTIME_ERROR;
        }
        cache->kind = CACHE_GLOBAL;
        cache->key = name;
        cache->entry = entry;
        cache->tableVersion = vm.globals.version();
      } else {
        recordGlobalCacheHit(vm);
      }

      entry->value = vm.stackTop[-1];
      break;
    }
    case OP_GET_UPVALUE: {
      uint8_t slot = readByte();
      pushValue(*frame->closure->upvalues[slot]->location);
      break;
    }
    case OP_SET_UPVALUE: {
      uint8_t slot = readByte();
      *frame->closure->upvalues[slot]->location = vm.stackTop[-1];
      break;
    }
    case OP_GET_PROPERTY: {
      if (!isInstance(peek(vm, 0))) {
        runtimeError(vm, "Only instances have properties.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjInstance *instance = asInstance(peek(vm, 0));
      uint8_t constant = readByte();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *name = asString(chunk->constantAt(constant));
      InlineCache *cache = &chunk->inlineCache(constant);

      if (cache->kind == CACHE_FIELD && cache->key == name &&
          cache->ownerClass == instance->klass &&
          cache->secondaryVersion == instance->klass->fieldVersion &&
          cache->entryIndex >= 0) {
        Value fieldValue;
        if (readInstanceField(instance, cache->entryIndex, &fieldValue)) {
          recordFieldCacheHit(vm);
          vm.stackTop[-1] = fieldValue;
          break;
        }
      }

      recordFieldCacheMiss(vm);
      int fieldSlot = -1;
      Value fieldValue;
      if (getFieldSlot(instance->klass, name, &fieldSlot) &&
          readInstanceField(instance, fieldSlot, &fieldValue)) {
        cache->kind = CACHE_FIELD;
        cache->key = name;
        cache->ownerClass = instance->klass;
        cache->entry = nullptr;
        cache->entryIndex = fieldSlot;
        cache->tableVersion = 0;
        cache->secondaryVersion = instance->klass->fieldVersion;
        vm.stackTop[-1] = fieldValue;
        break;
      }

      if (!bindMethodCached(vm, instance->klass, name, cache)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SET_PROPERTY: {
      if (!isInstance(peek(vm, 1))) {
        runtimeError(vm, "Only instances have fields.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjInstance *instance = asInstance(peek(vm, 1));
      int fieldSlot = ensureFieldSlot(instance->klass, readString());
      writeInstanceField(instance, fieldSlot, peek(vm, 0));
      Value value = popValue();
      popValue();
      pushValue(value);
      break;
    }
    case OP_GET_SUPER: {
      ObjString *name = readString();
      ObjClass *superclass = asClass(popValue());

      if (!bindMethod(vm, superclass, name)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_EQUAL: {
      bool equal = valuesEqual(vm.stackTop[-2], vm.stackTop[-1]);
      vm.stackTop[-2] = boolValue(equal);
      vm.stackTop--;
      break;
    }
    case OP_GREATER:
      if (!binaryOp(boolValue, [](double a, double b) { return a > b; }))
        return INTERPRET_RUNTIME_ERROR;
      break;
    case OP_LESS:
      if (!binaryOp(boolValue, [](double a, double b) { return a < b; }))
        return INTERPRET_RUNTIME_ERROR;
      break;

    case OP_ADD: {
      Value bValue = vm.stackTop[-1];
      Value aValue = vm.stackTop[-2];
      if (isString(bValue) && isString(aValue)) {
        concatenate(vm);
      } else if (isNumber(bValue) && isNumber(aValue)) {
        vm.stackTop[-2] = numberValue(asNumber(aValue) + asNumber(bValue));
        vm.stackTop--;
      } else {
        runtimeError(vm, "Operands must be two numbers or two strings.");
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SUBTRACT:
      if (!binaryOp(numberValue, [](double a, double b) { return a - b; }))
        return INTERPRET_RUNTIME_ERROR;
      break;
    case OP_MULTIPLY:
      if (!binaryOp(numberValue, [](double a, double b) { return a * b; }))
        return INTERPRET_RUNTIME_ERROR;
      break;
    case OP_DIVIDE:
      if (!binaryOp(numberValue, [](double a, double b) { return a / b; }))
        return INTERPRET_RUNTIME_ERROR;
      break;
    case OP_NOT:
      vm.stackTop[-1] = boolValue(isFalsey(vm.stackTop[-1]));
      break;
    case OP_NEGATE:
      if (!isNumber(vm.stackTop[-1])) {
        runtimeError(vm, "Operand must be a number.");
        return INTERPRET_RUNTIME_ERROR;
      }
      vm.stackTop[-1] = numberValue(-asNumber(vm.stackTop[-1]));
      break;
    case OP_PRINT: {
      printValue(std::cout, popValue());
      std::cout << '\n';
      break;
    }
    case OP_JUMP: {
      uint16_t offset = readShort();

      frame->ip += offset;
      break;
    }
    case OP_JUMP_IF_FALSE: {
      uint16_t offset = readShort();

      if (isFalsey(peek(vm, 0)))
        frame->ip += offset;
      break;
    }
    case OP_LOOP: {
      uint16_t offset = readShort();

      frame->ip -= offset;
      break;
    }
    case OP_CALL: {
      int argCount = readByte();
      if (!callValue(vm, peek(vm, argCount), argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_INVOKE: {
      uint8_t constant = readByte();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *method = asString(chunk->constantAt(constant));
      int argCount = readByte();
      if (!invoke(vm, method, argCount, &chunk->inlineCache(constant))) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_SUPER_INVOKE: {
      uint8_t constant = readByte();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *method = asString(chunk->constantAt(constant));
      int argCount = readByte();
      ObjClass *superclass = asClass(popValue());
      if (!invokeFromClass(vm, superclass, method, argCount,
                           &chunk->inlineCache(constant))) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_CLOSURE: {
      ObjFunction *function = asFunction(readConstant());
      ObjClosure *closure = vm.newClosure(function);
      pushValue(objectValue(closure));
      for (int i = 0; i < closure->upvalues.size(); i++) {
        uint8_t isLocal = readByte();
        uint8_t index = readByte();
        if (isLocal) {
          closure->upvalues[i] = captureUpvalue(vm, frame->slots + index);
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      break;
    }
    case OP_CLOSE_UPVALUE:
      closeUpvalues(vm, vm.stackTop - 1);
      vm.stackTop--;
      break;
    case OP_RETURN: {

      Value result = popValue();
      closeUpvalues(vm, frame->slots);
      vm.frameCount--;
      if (vm.frameCount == 0) {
        popValue();
        return INTERPRET_OK;
      }

      vm.stackTop = frame->slots;
      pushValue(result);
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_CLASS:
      pushValue(objectValue(vm.newClass(readString())));
      break;
    case OP_INHERIT: {
      Value superclass = peek(vm, 1);
      if (!isClass(superclass)) {
        runtimeError(vm, "Superclass must be a class.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjClass *subclass = asClass(peek(vm, 0));
      ObjClass *superKlass = asClass(superclass);
      subclass->methods.addAllFrom(superKlass->methods);
      subclass->initializer = superKlass->initializer;
      vm.stackTop--;
      break;
    }
    case OP_METHOD:
      defineMethod(vm, readString());
      break;
    }
  }
}

InterpretResult Vm::interpret(std::string_view source) {
  Vm &vm = *this;

  ObjFunction *function = compile(vm, source);
  if (function == nullptr)
    return INTERPRET_COMPILE_ERROR;

  vm.push(objectValue(function));

  ObjClosure *closure = vm.newClosure(function);
  vm.pop();
  vm.push(objectValue(closure));
  call(vm, closure, 0);

  return run(vm);
}

} // namespace cpplox
