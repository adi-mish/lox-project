#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

namespace cpplox {

inline constexpr int kMaxFrames = 64;
inline constexpr int kMaxStack = kMaxFrames * kUint8Count;

enum class InterpretResult : uint8_t {
  Ok,
  CompileError,
  RuntimeError
};

inline constexpr InterpretResult INTERPRET_OK = InterpretResult::Ok;
inline constexpr InterpretResult INTERPRET_COMPILE_ERROR =
    InterpretResult::CompileError;
inline constexpr InterpretResult INTERPRET_RUNTIME_ERROR =
    InterpretResult::RuntimeError;

struct CallFrame {
  ObjClosure *closure;
  uint8_t *ip;
  Value *slots;
};

class Vm {
public:
  Vm();
  ~Vm();

  Vm(const Vm &) = delete;
  Vm &operator=(const Vm &) = delete;

  InterpretResult interpret(std::string_view source);
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
  void addCompilerRoot(ObjFunction *function);
  void popCompilerRoot();
  void markCompilerRoots();

#ifdef CPPLOX_ENABLE_VM_STATS
  void setStatsEnabled(bool enabled);
  void resetStats();
  void printStats() const;
#endif

  std::array<CallFrame, kMaxFrames> frames;
  int frameCount;

  std::array<Value, kMaxStack> stack;
  Value *stackTop;
  Table globals;
  Table strings;
  ObjString *initString;
  ObjUpvalue *openUpvalues;

  Heap heap;
  std::vector<ObjFunction *> compilerRoots;
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

private:
  void initialize();
  void shutdown();
};

} // namespace cpplox
