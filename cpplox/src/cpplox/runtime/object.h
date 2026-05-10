#ifndef clox_object_h
#define clox_object_h

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

namespace cpplox {

#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_BOUND_METHOD(value) ((ObjBoundMethod *)AS_OBJ(value))
#define AS_CLASS(value) ((ObjClass *)AS_OBJ(value))
#define AS_CLOSURE(value) ((ObjClosure *)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction *)AS_OBJ(value))
#define AS_INSTANCE(value) ((ObjInstance *)AS_OBJ(value))
#define AS_NATIVE(value) (((ObjNative *)AS_OBJ(value))->function)
#define AS_STRING(value) ((ObjString *)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString *)AS_OBJ(value))->chars)

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
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

} // namespace cpplox

#endif
