#pragma once

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

namespace cpplox {

enum class ObjectKind : uint8_t {
  BoundMethod,
  Class,
  Closure,
  Function,
  Instance,
  Native,
  String,
  Upvalue
};

inline constexpr ObjectKind OBJ_BOUND_METHOD = ObjectKind::BoundMethod;
inline constexpr ObjectKind OBJ_CLASS = ObjectKind::Class;
inline constexpr ObjectKind OBJ_CLOSURE = ObjectKind::Closure;
inline constexpr ObjectKind OBJ_FUNCTION = ObjectKind::Function;
inline constexpr ObjectKind OBJ_INSTANCE = ObjectKind::Instance;
inline constexpr ObjectKind OBJ_NATIVE = ObjectKind::Native;
inline constexpr ObjectKind OBJ_STRING = ObjectKind::String;
inline constexpr ObjectKind OBJ_UPVALUE = ObjectKind::Upvalue;

inline constexpr int objectKindIndex(ObjectKind kind) {
  return static_cast<int>(kind);
}

struct Obj {
  ObjectKind type;
  bool isMarked;
  Obj *next;
};

struct ObjFunction {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString *name;
};

using NativeFn = Value (*)(int argCount, Value *args);

struct ObjNative {
  Obj obj;
  NativeFn function;
};

struct ObjString {
  Obj obj;
  int length;
  char *chars;
  uint32_t hash;
};
struct ObjUpvalue {
  Obj obj;
  Value *location;
  Value closed;
  ObjUpvalue *next;
};
struct ObjClosure {
  Obj obj;
  ObjFunction *function;
  ObjUpvalue **upvalues;
  int upvalueCount;
};

struct ObjClass {
  Obj obj;
  ObjString *name;
  Table methods;
  ObjClosure *initializer;
  Table fieldSlots;
  int fieldSlotCount;
  uint32_t fieldVersion;
};

struct ObjInstance {
  Obj obj;
  ObjClass *klass;
  Value *fields;
  int fieldCapacity;
};

struct ObjBoundMethod {
  Obj obj;
  Value receiver;
  ObjClosure *method;
};

void printObject(Value value);

static inline bool isObjType(Value value, ObjectKind type) {
  return isObj(value) && asObj(value)->type == type;
}

inline ObjectKind objectType(Value value) { return asObj(value)->type; }
inline bool isBoundMethod(Value value) {
  return isObjType(value, OBJ_BOUND_METHOD);
}
inline bool isClass(Value value) { return isObjType(value, OBJ_CLASS); }
inline bool isClosure(Value value) { return isObjType(value, OBJ_CLOSURE); }
inline bool isFunction(Value value) { return isObjType(value, OBJ_FUNCTION); }
inline bool isInstance(Value value) { return isObjType(value, OBJ_INSTANCE); }
inline bool isNative(Value value) { return isObjType(value, OBJ_NATIVE); }
inline bool isString(Value value) { return isObjType(value, OBJ_STRING); }

inline ObjBoundMethod *asBoundMethod(Value value) {
  return reinterpret_cast<ObjBoundMethod *>(asObj(value));
}
inline ObjClass *asClass(Value value) {
  return reinterpret_cast<ObjClass *>(asObj(value));
}
inline ObjClosure *asClosure(Value value) {
  return reinterpret_cast<ObjClosure *>(asObj(value));
}
inline ObjFunction *asFunction(Value value) {
  return reinterpret_cast<ObjFunction *>(asObj(value));
}
inline ObjInstance *asInstance(Value value) {
  return reinterpret_cast<ObjInstance *>(asObj(value));
}
inline NativeFn asNative(Value value) {
  return reinterpret_cast<ObjNative *>(asObj(value))->function;
}
inline ObjString *asString(Value value) {
  return reinterpret_cast<ObjString *>(asObj(value));
}
inline char *asCString(Value value) { return asString(value)->chars; }

} // namespace cpplox

