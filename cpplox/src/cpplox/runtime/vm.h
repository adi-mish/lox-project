#ifndef clox_vm_h
#define clox_vm_h

#include <array>

#include "object.h"
#include "table.h"
#include "value.h"

namespace cpplox {

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

struct CallFrame {
  ObjClosure *closure;
  uint8_t *ip;
  Value *slots;
};

class Vm {
public:
  void initialize();
  void shutdown();

  InterpretResult interpret(const char *source);
  void push(Value value);
  Value pop();

  ObjBoundMethod *newBoundMethod(Value receiver, ObjClosure *method);
  ObjClass *newClass(ObjString *name);
  ObjClosure *newClosure(ObjFunction *function);
  ObjFunction *newFunction();
  ObjInstance *newInstance(ObjClass *klass);
  ObjNative *newNative(NativeFn function);
  ObjString *takeString(char *chars, int length);
  ObjString *copyString(const char *chars, int length);
  ObjUpvalue *newUpvalue(Value *slot);

#ifdef CPPLOX_ENABLE_VM_STATS
  void setStatsEnabled(bool enabled);
  void resetStats();
  void printStats() const;
#endif

  std::array<CallFrame, FRAMES_MAX> frames;
  int frameCount;

  std::array<Value, STACK_MAX> stack;
  Value *stackTop;
  Table globals;
  Table strings;
  ObjString *initString;
  ObjUpvalue *openUpvalues;

  size_t bytesAllocated;
  size_t nextGC;
  Obj *objects;
  int grayCount;
  int grayCapacity;
  Obj **grayStack;
#ifdef CPPLOX_ENABLE_VM_STATS
  bool statsEnabled;
  uint64_t instructionsExecuted;
  std::array<uint64_t, OP_COUNT> opcodeCounts;
  uint64_t maxStackDepth;
  uint64_t closureCalls;
  uint64_t nativeCalls;
  uint64_t classCalls;
  uint64_t boundMethodCalls;
  uint64_t invokes;
  uint64_t globalCacheHits;
  uint64_t globalCacheMisses;
  uint64_t methodCacheHits;
  uint64_t methodCacheMisses;
  uint64_t fieldCacheHits;
  uint64_t fieldCacheMisses;
#endif
};

Vm &currentVm();

} // namespace cpplox

#endif
