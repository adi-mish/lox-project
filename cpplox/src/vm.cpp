#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef CPPLOX_ENABLE_VM_STATS
#include <inttypes.h>
#endif

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

VM vm;

#ifdef CPPLOX_ENABLE_VM_STATS
static const char *opcodeName(int opcode) {
  switch (opcode) {
  case OP_CONSTANT:
    return "OP_CONSTANT";
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

void setVMStatsEnabled(bool enabled) { vm.statsEnabled = enabled; }

void resetVMStats() {
  bool enabled = vm.statsEnabled;
  memset(vm.opcodeCounts, 0, sizeof(vm.opcodeCounts));
  vm.instructionsExecuted = 0;
  vm.maxStackDepth = 0;
  vm.closureCalls = 0;
  vm.nativeCalls = 0;
  vm.classCalls = 0;
  vm.boundMethodCalls = 0;
  vm.invokes = 0;
  vm.globalCacheHits = 0;
  vm.globalCacheMisses = 0;
  vm.statsEnabled = enabled;
}

static void recordInstruction(uint8_t opcode) {
  if (!vm.statsEnabled)
    return;
  vm.instructionsExecuted++;
  vm.opcodeCounts[opcode]++;
  uint64_t depth = (uint64_t)(vm.stackTop - vm.stack);
  if (depth > vm.maxStackDepth)
    vm.maxStackDepth = depth;
}

void printVMStats() {
  fprintf(stderr, "cpplox VM stats:\n");
  fprintf(stderr, "  instructions: %" PRIu64 "\n", vm.instructionsExecuted);
  fprintf(stderr, "  max_stack_depth: %" PRIu64 "\n", vm.maxStackDepth);
  fprintf(stderr, "  bytes_allocated: %zu\n", vm.bytesAllocated);
  fprintf(stderr, "  closure_calls: %" PRIu64 "\n", vm.closureCalls);
  fprintf(stderr, "  native_calls: %" PRIu64 "\n", vm.nativeCalls);
  fprintf(stderr, "  class_calls: %" PRIu64 "\n", vm.classCalls);
  fprintf(stderr, "  bound_method_calls: %" PRIu64 "\n", vm.boundMethodCalls);
  fprintf(stderr, "  invokes: %" PRIu64 "\n", vm.invokes);
  fprintf(stderr, "  global_cache_hits: %" PRIu64 "\n", vm.globalCacheHits);
  fprintf(stderr, "  global_cache_misses: %" PRIu64 "\n", vm.globalCacheMisses);
  fprintf(stderr, "  opcodes:\n");
  for (int i = 0; i < OP_COUNT; i++) {
    if (vm.opcodeCounts[i] == 0)
      continue;
    fprintf(stderr, "    %-20s %" PRIu64 "\n", opcodeName(i),
            vm.opcodeCounts[i]);
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
#else
#define RECORD_GLOBAL_CACHE_HIT() ((void)0)
#define RECORD_GLOBAL_CACHE_MISS() ((void)0)
#endif

static Value clockNative(int argCount, Value *args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}
static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}
static void runtimeError(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame *frame = &vm.frames[i];

    ObjFunction *function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  resetStack();
}
static void defineNative(const char *name, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

void initVM() {
  resetStack();
#ifdef CPPLOX_ENABLE_VM_STATS
  vm.statsEnabled = false;
  resetVMStats();
#endif
  vm.objects = NULL;
  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;

  initTable(&vm.globals);
  initTable(&vm.strings);

  vm.initString = NULL;
  vm.initString = copyString("init", 4);

  defineNative("clock", clockNative);
}

void freeVM() {
  freeTable(&vm.globals);
  freeTable(&vm.strings);
  vm.initString = NULL;
  freeObjects();
}
void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}
Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}
static Value peek(int distance) { return vm.stackTop[-1 - distance]; }

static bool call(ObjClosure *closure, int argCount) {

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
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}
static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod *bound = AS_BOUND_METHOD(callee);
#ifdef CPPLOX_ENABLE_VM_STATS
      if (vm.statsEnabled)
        vm.boundMethodCalls++;
#endif
      vm.stackTop[-argCount - 1] = bound->receiver;
      return call(bound->method, argCount);
    }
    case OBJ_CLASS: {
      ObjClass *klass = AS_CLASS(callee);
#ifdef CPPLOX_ENABLE_VM_STATS
      if (vm.statsEnabled)
        vm.classCalls++;
#endif
      vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
      Value initializer;
      if (tableGet(&klass->methods, vm.initString, &initializer)) {
        return call(AS_CLOSURE(initializer), argCount);
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
      return call(AS_CLOSURE(callee), argCount);

    case OBJ_NATIVE: {
      NativeFn native = AS_NATIVE(callee);
#ifdef CPPLOX_ENABLE_VM_STATS
      if (vm.statsEnabled)
        vm.nativeCalls++;
#endif
      Value result = native(argCount, vm.stackTop - argCount);
      vm.stackTop -= argCount + 1;
      push(result);
      return true;
    }
    default:
      break;
    }
  }
  runtimeError("Can only call functions and classes.");
  return false;
}
static bool invokeFromClass(ObjClass *klass, ObjString *name, int argCount) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }
  return call(AS_CLOSURE(method), argCount);
}
static bool invoke(ObjString *name, int argCount) {
#ifdef CPPLOX_ENABLE_VM_STATS
  if (vm.statsEnabled)
    vm.invokes++;
#endif
  Value receiver = peek(argCount);

  if (!IS_INSTANCE(receiver)) {
    runtimeError("Only instances have methods.");
    return false;
  }

  ObjInstance *instance = AS_INSTANCE(receiver);

  Value value;
  if (tableGet(&instance->fields, name, &value)) {
    vm.stackTop[-argCount - 1] = value;
    return callValue(value, argCount);
  }

  return invokeFromClass(instance->klass, name, argCount);
}
static bool bindMethod(ObjClass *klass, ObjString *name) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  ObjBoundMethod *bound = newBoundMethod(peek(0), AS_CLOSURE(method));
  pop();
  push(OBJ_VAL(bound));
  return true;
}
static ObjUpvalue *captureUpvalue(Value *local) {
  ObjUpvalue *prevUpvalue = NULL;
  ObjUpvalue *upvalue = vm.openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  ObjUpvalue *createdUpvalue = newUpvalue(local);
  createdUpvalue->next = upvalue;

  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}
static void closeUpvalues(Value *last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue *upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}
static void defineMethod(ObjString *name) {
  Value method = peek(0);
  ObjClass *klass = AS_CLASS(peek(1));
  tableSet(&klass->methods, name, method);
  pop();
}
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}
static void concatenate() {

  ObjString *b = AS_STRING(peek(0));
  ObjString *a = AS_STRING(peek(1));

  int length = a->length + b->length;
  char *chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString *result = takeString(chars, length);
  pop();
  pop();
  push(OBJ_VAL(result));
}
static InterpretResult run() {
  CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)

#define READ_SHORT()                                                           \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT()                                                        \
  (frame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())
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
    if (!IS_NUMBER(bValue) || !IS_NUMBER(aValue)) {                            \
      runtimeError("Operands must be numbers.");                               \
      return INTERPRET_RUNTIME_ERROR;                                          \
    }                                                                          \
    vm.stackTop[-2] = valueType(AS_NUMBER(aValue) op AS_NUMBER(bValue));       \
    vm.stackTop--;                                                             \
  } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");

    disassembleInstruction(
        &frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif

    uint8_t instruction = READ_BYTE();
    recordInstruction(instruction);
    switch (instruction) {
    case OP_CONSTANT: {
      Value constant = READ_CONSTANT();

      PUSH_VALUE(constant);
      break;
    }
    case OP_NIL:
      PUSH_VALUE(NIL_VAL);
      break;
    case OP_TRUE:
      PUSH_VALUE(BOOL_VAL(true));
      break;
    case OP_FALSE:
      PUSH_VALUE(BOOL_VAL(false));
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
      ObjString *name = AS_STRING(chunk->constants.values[constant]);
      GlobalCache *cache = &chunk->globalCaches[constant];

      Entry *entry = cache->entry;
      if (cache->key == name && cache->tableVersion == vm.globals.version &&
          entry != NULL && entry->key == name) {
        RECORD_GLOBAL_CACHE_HIT();
        PUSH_VALUE(entry->value);
        break;
      }

      RECORD_GLOBAL_CACHE_MISS();
      entry = tableGetEntry(&vm.globals, name);
      if (entry == NULL) {
        runtimeError("Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }

      cache->key = name;
      cache->entry = entry;
      cache->tableVersion = vm.globals.version;
      PUSH_VALUE(entry->value);
      break;
    }
    case OP_DEFINE_GLOBAL: {
      uint8_t constant = READ_BYTE();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *name = AS_STRING(chunk->constants.values[constant]);
      tableSet(&vm.globals, name, vm.stackTop[-1]);
      GlobalCache *cache = &chunk->globalCaches[constant];
      cache->key = name;
      cache->entry = tableGetEntry(&vm.globals, name);
      cache->tableVersion = vm.globals.version;
      vm.stackTop--;
      break;
    }
    case OP_SET_GLOBAL: {
      uint8_t constant = READ_BYTE();
      Chunk *chunk = &frame->closure->function->chunk;
      ObjString *name = AS_STRING(chunk->constants.values[constant]);
      GlobalCache *cache = &chunk->globalCaches[constant];

      Entry *entry = cache->entry;
      if (!(cache->key == name && cache->tableVersion == vm.globals.version &&
            entry != NULL && entry->key == name)) {
        RECORD_GLOBAL_CACHE_MISS();
        entry = tableGetEntry(&vm.globals, name);
        if (entry == NULL) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        cache->key = name;
        cache->entry = entry;
        cache->tableVersion = vm.globals.version;
      } else {
        RECORD_GLOBAL_CACHE_HIT();
      }

      if (entry == NULL) {
        runtimeError("Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
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
      if (!IS_INSTANCE(peek(0))) {
        runtimeError("Only instances have properties.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjInstance *instance = AS_INSTANCE(peek(0));
      ObjString *name = READ_STRING();

      Value value;
      if (tableGet(&instance->fields, name, &value)) {
        vm.stackTop[-1] = value;
        break;
      }

      if (!bindMethod(instance->klass, name)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SET_PROPERTY: {
      if (!IS_INSTANCE(peek(1))) {
        runtimeError("Only instances have fields.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjInstance *instance = AS_INSTANCE(peek(1));
      tableSet(&instance->fields, READ_STRING(), peek(0));
      Value value = POP_VALUE();
      POP_VALUE();
      PUSH_VALUE(value);
      break;
    }
    case OP_GET_SUPER: {
      ObjString *name = READ_STRING();
      ObjClass *superclass = AS_CLASS(POP_VALUE());

      if (!bindMethod(superclass, name)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_EQUAL: {
      bool equal = valuesEqual(vm.stackTop[-2], vm.stackTop[-1]);
      vm.stackTop[-2] = BOOL_VAL(equal);
      vm.stackTop--;
      break;
    }
    case OP_GREATER:
      BINARY_OP(BOOL_VAL, >);
      break;
    case OP_LESS:
      BINARY_OP(BOOL_VAL, <);
      break;

    case OP_ADD: {
      Value bValue = vm.stackTop[-1];
      Value aValue = vm.stackTop[-2];
      if (IS_STRING(bValue) && IS_STRING(aValue)) {
        concatenate();
      } else if (IS_NUMBER(bValue) && IS_NUMBER(aValue)) {
        vm.stackTop[-2] = NUMBER_VAL(AS_NUMBER(aValue) + AS_NUMBER(bValue));
        vm.stackTop--;
      } else {
        runtimeError("Operands must be two numbers or two strings.");
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SUBTRACT:
      BINARY_OP(NUMBER_VAL, -);
      break;
    case OP_MULTIPLY:
      BINARY_OP(NUMBER_VAL, *);
      break;
    case OP_DIVIDE:
      BINARY_OP(NUMBER_VAL, /);
      break;
    case OP_NOT:
      vm.stackTop[-1] = BOOL_VAL(isFalsey(vm.stackTop[-1]));
      break;
    case OP_NEGATE:
      if (!IS_NUMBER(vm.stackTop[-1])) {
        runtimeError("Operand must be a number.");
        return INTERPRET_RUNTIME_ERROR;
      }
      vm.stackTop[-1] = NUMBER_VAL(-AS_NUMBER(vm.stackTop[-1]));
      break;
    case OP_PRINT: {
      printValue(POP_VALUE());
      printf("\n");
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
      ObjString *method = READ_STRING();
      int argCount = READ_BYTE();
      if (!invoke(method, argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_SUPER_INVOKE: {
      ObjString *method = READ_STRING();
      int argCount = READ_BYTE();
      ObjClass *superclass = AS_CLASS(POP_VALUE());
      if (!invokeFromClass(superclass, method, argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_CLOSURE: {
      ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
      ObjClosure *closure = newClosure(function);
      PUSH_VALUE(OBJ_VAL(closure));
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
      PUSH_VALUE(OBJ_VAL(newClass(READ_STRING())));
      break;
    case OP_INHERIT: {
      Value superclass = peek(1);
      if (!IS_CLASS(superclass)) {
        runtimeError("Superclass must be a class.");
        return INTERPRET_RUNTIME_ERROR;
      }

      ObjClass *subclass = AS_CLASS(peek(0));
      tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
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

InterpretResult interpret(const char *source) {

  ObjFunction *function = compile(source);
  if (function == NULL)
    return INTERPRET_COMPILE_ERROR;

  push(OBJ_VAL(function));

  ObjClosure *closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  call(closure, 0);

  return run();
}
