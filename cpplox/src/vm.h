#ifndef clox_vm_h
#define clox_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {

  ObjClosure *closure;
  uint8_t *ip;
  Value *slots;
} CallFrame;

typedef struct {

  CallFrame frames[FRAMES_MAX];
  int frameCount;

  Value stack[STACK_MAX];
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
  uint64_t opcodeCounts[OP_COUNT];
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
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();

InterpretResult interpret(const char *source);
void push(Value value);
Value pop();

#ifdef CPPLOX_ENABLE_VM_STATS
void setVMStatsEnabled(bool enabled);
void resetVMStats();
void printVMStats();
#endif

#endif
