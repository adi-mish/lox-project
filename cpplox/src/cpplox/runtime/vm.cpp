#include <assert.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#ifdef CPPLOX_ENABLE_VM_STATS
#include <inttypes.h>
#endif

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

namespace cpplox {

static Vm *activeVm = nullptr;

Vm &currentVm() {
  assert(activeVm != nullptr);
  return *activeVm;
}

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

static void recordInstruction(uint8_t opcode) {
  Vm &vm = currentVm();
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
  std::fprintf(stderr, "  bytes_allocated: %zu\n", bytesAllocated);
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
#define recordInstruction(opcode) ((void)0)
#endif

#ifdef CPPLOX_ENABLE_VM_STATS
#define RECORD_GLOBAL_CACHE_HIT()                                              \
  do {                                                                         \
    if (vm.statsEnabled)                                                       \
      vm.globalCacheHits++;                                                    \
  } while (false)
#define RECORD_GLOBAL_CACHE_MISS()                                             \
  do {                                                                         \
    if (vm.statsEnabled)                                                       \
      vm.globalCacheMisses++;                                                  \
  } while (false)
#define RECORD_METHOD_CACHE_HIT()                                              \
  do {                                                                         \
    if (vm.statsEnabled)                                                       \
      vm.methodCacheHits++;                                                    \
  } while (false)
#define RECORD_METHOD_CACHE_MISS()                                             \
  do {                                                                         \
    if (vm.statsEnabled)                                                       \
      vm.methodCacheMisses++;                                                  \
  } while (false)
#define RECORD_FIELD_CACHE_HIT()                                               \
  do {                                                                         \
    if (vm.statsEnabled)                                                       \
      vm.fieldCacheHits++;                                                     \
  } while (false)
#define RECORD_FIELD_CACHE_MISS()                                              \
  do {                                                                         \
    if (vm.statsEnabled)                                                       \
      vm.fieldCacheMisses++;                                                   \
  } while (false)
#else
#define RECORD_GLOBAL_CACHE_HIT() ((void)0)
#define RECORD_GLOBAL_CACHE_MISS() ((void)0)
#define RECORD_METHOD_CACHE_HIT() ((void)0)
#define RECORD_METHOD_CACHE_MISS() ((void)0)
#define RECORD_FIELD_CACHE_HIT() ((void)0)
#define RECORD_FIELD_CACHE_MISS() ((void)0)
#endif

static Value clockNative(int argCount, Value *args) {
  return numberValue((double)std::clock() / CLOCKS_PER_SEC);
}
static void resetStack() {
  Vm &vm = currentVm();
  vm.stackTop = vm.stack.data();
  vm.frameCount = 0;
  vm.openUpvalues = nullptr;
}
static void runtimeError(const char *format, ...) {
  Vm &vm = currentVm();
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame *frame = &vm.frames[i];

    ObjFunction *function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.codeData() - 1;
    std::fprintf(stderr, "[line %d] in ", function->chunk.lineAt(instruction));
    if (function->name == nullptr) {
      std::fprintf(stderr, "script\n");
    } else {
      std::fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  resetStack();
}
static void defineNative(const char *name, NativeFn function) {
  Vm &vm = currentVm();
  vm.push(objectValue(vm.copyString(name, (int)std::strlen(name))));
  vm.push(objectValue(vm.newNative(function)));
  vm.globals.set(asString(vm.stack[0]), vm.stack[1]);
  vm.pop();
  vm.pop();
}

void Vm::initialize() {
  activeVm = this;
  Vm &vm = *this;
  resetStack();
#ifdef CPPLOX_ENABLE_VM_STATS
  vm.statsEnabled = false;
  resetStats();
#endif
  vm.objects = nullptr;
  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayStack.clear();

  vm.globals.clear();
  vm.strings.clear();

  vm.initString = nullptr;
  vm.initString = vm.copyString("init", 4);

  defineNative("clock", clockNative);
}

void Vm::shutdown() {
  Vm &vm = *this;
  vm.globals.clear();
  vm.strings.clear();
  vm.initString = nullptr;
  freeObjects();
  activeVm = nullptr;
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
    markObject(reinterpret_cast<Obj *>(function));
  }
}

static Value peek(int distance) {
  Vm &vm = currentVm();
  return vm.stackTop[-1 - distance];
}

static bool call(ObjClosure *closure, int argCount) {
  Vm &vm = currentVm();

  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.", closure->function->arity,
                 argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame *frame = &vm.frames[vm.frameCount++];

  frame->closure = closure;
  frame->ip = closure->function->chunk.codeData();
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}
static bool callValue(Value callee, int argCount) {
  Vm &vm = currentVm();
  if (isObj(callee)) {
    switch (objectType(callee)) {
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod *bound = asBoundMethod(callee);
#ifdef CPPLOX_ENABLE_VM_STATS
      if (vm.statsEnabled)
        vm.boundMethodCalls++;
#endif
      vm.stackTop[-argCount - 1] = bound->receiver;
      return call(bound->method, argCount);
    }
    case OBJ_CLASS: {
      ObjClass *klass = asClass(callee);
#ifdef CPPLOX_ENABLE_VM_STATS
      if (vm.statsEnabled)
        vm.classCalls++;
#endif
      vm.stackTop[-argCount - 1] = objectValue(vm.newInstance(klass));
      if (klass->initializer != nullptr) {
        return call(klass->initializer, argCount);
      } else if (argCount != 0) {
        runtimeError("Expected 0 arguments but got %d.", argCount);
        return false;
      }
      return true;
    }
    case OBJ_CLOSURE:
#ifdef CPPLOX_ENABLE_VM_STATS
      if (vm.statsEnabled)
        vm.closureCalls++;
#endif
      return call(asClosure(callee), argCount);

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
  runtimeError("Can only call functions and classes.");
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

static void ensureInstanceFieldCapacity(ObjInstance *instance, int slot) {
  if (slot < instance->fieldCapacity)
    return;

  int oldCapacity = instance->fieldCapacity;
  int newCapacity = growCapacity(oldCapacity);
  while (newCapacity <= slot) {
    newCapacity = growCapacity(newCapacity);
  }

  instance->fields = growArray(instance->fields, oldCapacity, newCapacity);
  for (int i = oldCapacity; i < newCapacity; i++) {
    instance->fields[i] = uninitializedValue();
  }
  instance->fieldCapacity = newCapacity;
}

static bool readInstanceField(ObjInstance *instance, int slot, Value *value) {
  if (slot < 0 || slot >= instance->fieldCapacity ||
      isUninitialized(instance->fields[slot])) {
    return false;
  }
  *value = instance->fields[slot];
  return true;
}

static void writeInstanceField(ObjInstance *instance, int slot, Value value) {
  ensureInstanceFieldCapacity(instance, slot);
  instance->fields[slot] = value;
}

static bool findMethodCached(ObjClass *klass, ObjString *name,
                             InlineCache *cache, Value *method) {
#ifdef CPPLOX_ENABLE_VM_STATS
  Vm &vm = currentVm();
#endif
  if (cache != nullptr && cache->kind == CACHE_METHOD && cache->key == name &&
      cache->owner == klass &&
      cache->tableVersion == klass->methods.version()) {
    RECORD_METHOD_CACHE_HIT();
    *method = cache->value;
    return true;
  }

  RECORD_METHOD_CACHE_MISS();
  if (!klass->methods.get(name, method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  if (cache != nullptr) {
    cache->kind = CACHE_METHOD;
    cache->key = name;
    cache->owner = klass;
    cache->value = *method;
    cache->tableVersion = klass->methods.version();
    cache->secondaryOwner = nullptr;
    cache->secondaryVersion = 0;
    cache->entryIndex = -2;
    cache->tableCapacity = -1;
  }
  return true;
}

static bool invokeFromClass(ObjClass *klass, ObjString *name, int argCount,
                            InlineCache *cache) {
  Value method;
  if (!findMethodCached(klass, name, cache, &method))
    return false;
  return call(asClosure(method), argCount);
}

static bool invoke(ObjString *name, int argCount, InlineCache *cache) {
  Vm &vm = currentVm();
#ifdef CPPLOX_ENABLE_VM_STATS
  if (vm.statsEnabled)
    vm.invokes++;
#endif
  Value receiver = peek(argCount);

  if (!isInstance(receiver)) {
    runtimeError("Only instances have methods.");
    return false;
  }

  ObjInstance *instance = asInstance(receiver);
  if (cache != nullptr && cache->kind == CACHE_METHOD && cache->key == name &&
      cache->owner == instance->klass &&
      cache->tableVersion == instance->klass->methods.version() &&
      cache->secondaryVersion == instance->klass->fieldVersion) {
    if (cache->entryIndex == -1) {
      RECORD_METHOD_CACHE_HIT();
      return call(asClosure(cache->value), argCount);
    }
    if (cache->entryIndex >= 0) {
      Value ignored;
      if (!readInstanceField(instance, cache->entryIndex, &ignored)) {
        RECORD_METHOD_CACHE_HIT();
        return call(asClosure(cache->value), argCount);
      }
    }
  }

  Value value;
  int fieldSlot = -1;
  if (getFieldSlot(instance->klass, name, &fieldSlot) &&
      readInstanceField(instance, fieldSlot, &value)) {
    vm.stackTop[-argCount - 1] = value;
    return callValue(value, argCount);
  }

  if (!findMethodCached(instance->klass, name, cache, &value))
    return false;
  if (cache != nullptr) {
    cache->secondaryVersion = instance->klass->fieldVersion;
    cache->entryIndex = fieldSlot >= 0 ? fieldSlot : -1;
  }
  return call(asClosure(value), argCount);
}
static bool bindMethodCached(ObjClass *klass, ObjString *name,
                             InlineCache *cache) {
  Value method;
  if (!findMethodCached(klass, name, cache, &method))
    return false;

  ObjBoundMethod *bound = currentVm().newBoundMethod(peek(0), asClosure(method));
  currentVm().pop();
  currentVm().push(objectValue(bound));
  return true;
}
static bool bindMethod(ObjClass *klass, ObjString *name) {
  return bindMethodCached(klass, name, nullptr);
}
static ObjUpvalue *captureUpvalue(Value *local) {
  Vm &vm = currentVm();
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
static void closeUpvalues(Value *last) {
  Vm &vm = currentVm();
  while (vm.openUpvalues != nullptr && vm.openUpvalues->location >= last) {
    ObjUpvalue *upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}
static void defineMethod(ObjString *name) {
  Vm &vm = currentVm();
  Value method = peek(0);
  ObjClass *klass = asClass(peek(1));
  klass->methods.set(name, method);
  if (name == vm.initString) {
    klass->initializer = asClosure(method);
  }
  vm.pop();
}
static bool isFalsey(Value value) {
  return isNil(value) || (isBool(value) && !asBool(value));
}
static void concatenate() {
  Vm &vm = currentVm();

  ObjString *b = asString(peek(0));
  ObjString *a = asString(peek(1));

  int length = a->length + b->length;
  char *chars = allocate<char>(length + 1);
  std::memcpy(chars, a->chars, a->length);
  std::memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString *result = vm.takeString(chars, length);
  vm.pop();
  vm.pop();
  vm.push(objectValue(result));
}
static InterpretResult run() {
  Vm &vm = currentVm();
  CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)

#define READ_SHORT()                                                           \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT()                                                        \
  (frame->closure->function->chunk.constantAt(READ_BYTE()))

#define READ_STRING() asString(READ_CONSTANT())
#define PUSH_VALUE(value)                                                      \
  do {                                                                         \
    *vm.stackTop = (value);                                                    \
    vm.stackTop++;                                                             \
  } while (false)
#define POP_VALUE() (*--vm.stackTop)

#define BINARY_OP(valueType, op)                                               \
  do {                                                                         \
    Value bValue = vm.stackTop[-1];                                            \
    Value aValue = vm.stackTop[-2];                                            \
    if (!isNumber(bValue) || !isNumber(aValue)) {                            \
      runtimeError("Operands must be numbers.");                               \
      return INTERPRET_RUNTIME_ERROR;                                          \
    }                                                                          \
    vm.stackTop[-2] = valueType(asNumber(aValue) op asNumber(bValue));       \
    vm.stackTop--;                                                             \
  } while (false)

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

    uint8_t instruction = READ_BYTE();
    recordInstruction(instruction);
    switch (instruction) {
    case OP_CONSTANT: {
      Value constant = READ_CONSTANT();

      PUSH_VALUE(constant);
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
      PUSH_VALUE(constants[instruction - OP_CONSTANT_0]);
      break;
    }
    case OP_NIL:
      PUSH_VALUE(nilValue());
      break;
    case OP_TRUE:
      PUSH_VALUE(boolValue(true));
      break;
    case OP_FALSE:
      PUSH_VALUE(boolValue(false));
      break;
    case OP_POP:
      vm.stackTop--;
      break;
    case OP_GET_LOCAL: {
      uint8_t slot = READ_BYTE();

      PUSH_VALUE(frame->slots[slot]);
      break;
    }
    case OP_GET_LOCAL_0:
      PUSH_VALUE(frame->slots[0]);
      break;
    case OP_GET_LOCAL_1:
      PUSH_VALUE(frame->slots[1]);
      break;
    case OP_GET_LOCAL_2:
      PUSH_VALUE(frame->slots[2]);
      break;
    case OP_GET_LOCAL_3:
      PUSH_VALUE(frame->slots[3]);
      break;
    case OP_GET_LOCAL_4:
      PUSH_VALUE(frame->slots[4]);
      break;
    case OP_GET_LOCAL_5:
      PUSH_VALUE(frame->slots[5]);
      break;
    case OP_GET_LOCAL_6:
      PUSH_VALUE(frame->slots[6]);
      break;
    case OP_GET_LOCAL_7:
      PUSH_VALUE(frame->slots[7]);
      break;
    case OP_SET_LOCAL: {
      uint8_t slot = READ_BYTE();

      frame->slots[slot] = peek(0);
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
      uint8_t constant = READ_BYTE();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *name = asString(chunk->constantAt(constant));
      InlineCache *cache = &chunk->inlineCache(constant);

      Entry *entry = cache->entry;
      if (cache->kind == CACHE_GLOBAL && cache->key == name &&
          cache->tableVersion == vm.globals.version() && entry != nullptr) {
        RECORD_GLOBAL_CACHE_HIT();
        PUSH_VALUE(entry->value);
        break;
      }

      RECORD_GLOBAL_CACHE_MISS();
      entry = vm.globals.getEntry(name);
      if (entry == nullptr) {
        runtimeError("Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }

      cache->key = name;
      cache->entry = entry;
      cache->tableVersion = vm.globals.version();
      cache->kind = CACHE_GLOBAL;
      PUSH_VALUE(entry->value);
      break;
    }
    case OP_DEFINE_GLOBAL: {
      uint8_t constant = READ_BYTE();
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
      uint8_t constant = READ_BYTE();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *name = asString(chunk->constantAt(constant));
      InlineCache *cache = &chunk->inlineCache(constant);

      Entry *entry = cache->entry;
      if (!(cache->kind == CACHE_GLOBAL && cache->key == name &&
            cache->tableVersion == vm.globals.version() && entry != nullptr)) {
        RECORD_GLOBAL_CACHE_MISS();
        entry = vm.globals.getEntry(name);
        if (entry == nullptr) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        cache->kind = CACHE_GLOBAL;
        cache->key = name;
        cache->entry = entry;
        cache->tableVersion = vm.globals.version();
      } else {
        RECORD_GLOBAL_CACHE_HIT();
      }

      entry->value = vm.stackTop[-1];
      break;
    }
    case OP_GET_UPVALUE: {
      uint8_t slot = READ_BYTE();
      PUSH_VALUE(*frame->closure->upvalues[slot]->location);
      break;
    }
    case OP_SET_UPVALUE: {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = vm.stackTop[-1];
      break;
    }
    case OP_GET_PROPERTY: {
      if (!isInstance(peek(0))) {
        runtimeError("Only instances have properties.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjInstance *instance = asInstance(peek(0));
      uint8_t constant = READ_BYTE();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *name = asString(chunk->constantAt(constant));
      InlineCache *cache = &chunk->inlineCache(constant);

      if (cache->kind == CACHE_FIELD && cache->key == name &&
          cache->owner == instance->klass &&
          cache->secondaryVersion == instance->klass->fieldVersion &&
          cache->entryIndex >= 0) {
        Value fieldValue;
        if (readInstanceField(instance, cache->entryIndex, &fieldValue)) {
          RECORD_FIELD_CACHE_HIT();
          vm.stackTop[-1] = fieldValue;
          break;
        }
      }

      RECORD_FIELD_CACHE_MISS();
      int fieldSlot = -1;
      Value fieldValue;
      if (getFieldSlot(instance->klass, name, &fieldSlot) &&
          readInstanceField(instance, fieldSlot, &fieldValue)) {
        cache->kind = CACHE_FIELD;
        cache->key = name;
        cache->owner = instance->klass;
        cache->entry = nullptr;
        cache->entryIndex = fieldSlot;
        cache->tableCapacity = -1;
        cache->tableVersion = 0;
        cache->secondaryVersion = instance->klass->fieldVersion;
        vm.stackTop[-1] = fieldValue;
        break;
      }

      if (!bindMethodCached(instance->klass, name, cache)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SET_PROPERTY: {
      if (!isInstance(peek(1))) {
        runtimeError("Only instances have fields.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjInstance *instance = asInstance(peek(1));
      int fieldSlot = ensureFieldSlot(instance->klass, READ_STRING());
      writeInstanceField(instance, fieldSlot, peek(0));
      Value value = POP_VALUE();
      POP_VALUE();
      PUSH_VALUE(value);
      break;
    }
    case OP_GET_SUPER: {
      ObjString *name = READ_STRING();
      ObjClass *superclass = asClass(POP_VALUE());

      if (!bindMethod(superclass, name)) {
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
      BINARY_OP(boolValue, >);
      break;
    case OP_LESS:
      BINARY_OP(boolValue, <);
      break;

    case OP_ADD: {
      Value bValue = vm.stackTop[-1];
      Value aValue = vm.stackTop[-2];
      if (isString(bValue) && isString(aValue)) {
        concatenate();
      } else if (isNumber(bValue) && isNumber(aValue)) {
        vm.stackTop[-2] = numberValue(asNumber(aValue) + asNumber(bValue));
        vm.stackTop--;
      } else {
        runtimeError("Operands must be two numbers or two strings.");
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SUBTRACT:
      BINARY_OP(numberValue, -);
      break;
    case OP_MULTIPLY:
      BINARY_OP(numberValue, *);
      break;
    case OP_DIVIDE:
      BINARY_OP(numberValue, /);
      break;
    case OP_NOT:
      vm.stackTop[-1] = boolValue(isFalsey(vm.stackTop[-1]));
      break;
    case OP_NEGATE:
      if (!isNumber(vm.stackTop[-1])) {
        runtimeError("Operand must be a number.");
        return INTERPRET_RUNTIME_ERROR;
      }
      vm.stackTop[-1] = numberValue(-asNumber(vm.stackTop[-1]));
      break;
    case OP_PRINT: {
      printValue(POP_VALUE());
      std::printf("\n");
      break;
    }
    case OP_JUMP: {
      uint16_t offset = READ_SHORT();

      frame->ip += offset;
      break;
    }
    case OP_JUMP_IF_FALSE: {
      uint16_t offset = READ_SHORT();

      if (isFalsey(peek(0)))
        frame->ip += offset;
      break;
    }
    case OP_LOOP: {
      uint16_t offset = READ_SHORT();

      frame->ip -= offset;
      break;
    }
    case OP_CALL: {
      int argCount = READ_BYTE();
      if (!callValue(peek(argCount), argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_INVOKE: {
      uint8_t constant = READ_BYTE();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *method = asString(chunk->constantAt(constant));
      int argCount = READ_BYTE();
      if (!invoke(method, argCount, &chunk->inlineCache(constant))) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_SUPER_INVOKE: {
      uint8_t constant = READ_BYTE();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *method = asString(chunk->constantAt(constant));
      int argCount = READ_BYTE();
      ObjClass *superclass = asClass(POP_VALUE());
      if (!invokeFromClass(superclass, method, argCount,
                           &chunk->inlineCache(constant))) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_CLOSURE: {
      ObjFunction *function = asFunction(READ_CONSTANT());
      ObjClosure *closure = vm.newClosure(function);
      PUSH_VALUE(objectValue(closure));
      for (int i = 0; i < closure->upvalueCount; i++) {
        uint8_t isLocal = READ_BYTE();
        uint8_t index = READ_BYTE();
        if (isLocal) {
          closure->upvalues[i] = captureUpvalue(frame->slots + index);
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      break;
    }
    case OP_CLOSE_UPVALUE:
      closeUpvalues(vm.stackTop - 1);
      vm.stackTop--;
      break;
    case OP_RETURN: {

      Value result = POP_VALUE();
      closeUpvalues(frame->slots);
      vm.frameCount--;
      if (vm.frameCount == 0) {
        POP_VALUE();
        return INTERPRET_OK;
      }

      vm.stackTop = frame->slots;
      PUSH_VALUE(result);
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_CLASS:
      PUSH_VALUE(objectValue(vm.newClass(READ_STRING())));
      break;
    case OP_INHERIT: {
      Value superclass = peek(1);
      if (!isClass(superclass)) {
        runtimeError("Superclass must be a class.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjClass *subclass = asClass(peek(0));
      ObjClass *superKlass = asClass(superclass);
      subclass->methods.addAllFrom(superKlass->methods);
      subclass->initializer = superKlass->initializer;
      vm.stackTop--;
      break;
    }
    case OP_METHOD:
      defineMethod(READ_STRING());
      break;
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef PUSH_VALUE
#undef POP_VALUE
#undef BINARY_OP
}

InterpretResult Vm::interpret(const char *source) {
  Vm &vm = *this;

  ObjFunction *function = compile(vm, source);
  if (function == nullptr)
    return INTERPRET_COMPILE_ERROR;

  vm.push(objectValue(function));

  ObjClosure *closure = vm.newClosure(function);
  vm.pop();
  vm.push(objectValue(closure));
  call(closure, 0);

  return run();
}

} // namespace cpplox
