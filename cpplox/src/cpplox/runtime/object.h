#pragma once

#include <iosfwd>

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

namespace cpplox {

class Vm;

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

struct ObjFunction : Obj {
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString *name;
};

using NativeFn = Value (*)(int argCount, Value *args);

struct ObjNative : Obj {
  NativeFn function;
};

struct ObjString : Obj {
  int length;
  char *chars;
  uint32_t hash;
};
struct ObjUpvalue : Obj {
  Value *location;
  Value closed;
  ObjUpvalue *next;
};

class UpvalueStorage {
public:
  UpvalueStorage() = default;
  ~UpvalueStorage();
  UpvalueStorage(const UpvalueStorage &) = delete;
  UpvalueStorage &operator=(const UpvalueStorage &) = delete;

  void adopt(Vm &vm, ObjUpvalue **values, int count);
  ObjUpvalue *&operator[](int index) { return values_[index]; }
  ObjUpvalue *operator[](int index) const { return values_[index]; }
  int size() const { return count_; }

private:
  Vm *vm_ = nullptr;
  ObjUpvalue **values_ = nullptr;
  int count_ = 0;
};

struct ObjClosure : Obj {
  ObjFunction *function;
  UpvalueStorage upvalues;
};

struct ObjClass : Obj {
  ObjString *name;
  Table methods;
  ObjClosure *initializer;
  Table fieldSlots;
  int fieldSlotCount;
  uint32_t fieldVersion;
};

class FieldStorage {
public:
  FieldStorage() = default;
  ~FieldStorage();
  FieldStorage(const FieldStorage &) = delete;
  FieldStorage &operator=(const FieldStorage &) = delete;

  void initialize(Vm &vm);
  bool read(int slot, Value *value) const;
  void write(int slot, Value value);
  int capacity() const { return capacity_; }
  Value *data() { return values_; }
  const Value *data() const { return values_; }

private:
  void ensureCapacity(int slot);

  Vm *vm_ = nullptr;
  Value *values_ = nullptr;
  int capacity_ = 0;
};

struct ObjInstance : Obj {
  ObjClass *klass;
  FieldStorage fields;
};

struct ObjBoundMethod : Obj {
  Value receiver;
  ObjClosure *method;
};

void printObject(std::ostream &out, Value value);
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
  return static_cast<ObjBoundMethod *>(asObj(value));
}
inline ObjClass *asClass(Value value) {
  return static_cast<ObjClass *>(asObj(value));
}
inline ObjClosure *asClosure(Value value) {
  return static_cast<ObjClosure *>(asObj(value));
}
inline ObjFunction *asFunction(Value value) {
  return static_cast<ObjFunction *>(asObj(value));
}
inline ObjInstance *asInstance(Value value) {
  return static_cast<ObjInstance *>(asObj(value));
}
inline NativeFn asNative(Value value) {
  return static_cast<ObjNative *>(asObj(value))->function;
}
inline ObjString *asString(Value value) {
  return static_cast<ObjString *>(asObj(value));
}
inline char *asCString(Value value) { return asString(value)->chars; }

} // namespace cpplox
