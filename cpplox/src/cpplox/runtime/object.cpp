#include <cstdio>
#include <cstring>

#include <iostream>
#include <new>
#include <ostream>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

namespace cpplox {

UpvalueStorage::~UpvalueStorage() {
  if (vm_ != nullptr && values_ != nullptr) {
    freeArray(*vm_, values_, count_);
  }
}

void UpvalueStorage::adopt(Vm &vm, ObjUpvalue **values, int count) {
  vm_ = &vm;
  values_ = values;
  count_ = count;
}

FieldStorage::~FieldStorage() {
  if (vm_ != nullptr && values_ != nullptr) {
    freeArray(*vm_, values_, capacity_);
  }
}

void FieldStorage::initialize(Vm &vm) { vm_ = &vm; }

void FieldStorage::ensureCapacity(int slot) {
  if (slot < capacity_)
    return;

  int oldCapacity = capacity_;
  int newCapacity = growCapacity(oldCapacity);
  while (newCapacity <= slot) {
    newCapacity = growCapacity(newCapacity);
  }

  values_ = growArray(*vm_, values_, oldCapacity, newCapacity);
  for (int i = oldCapacity; i < newCapacity; i++) {
    values_[i] = uninitializedValue();
  }
  capacity_ = newCapacity;
}

bool FieldStorage::read(int slot, Value *value) const {
  if (slot < 0 || slot >= capacity_ || isUninitialized(values_[slot])) {
    return false;
  }
  *value = values_[slot];
  return true;
}

void FieldStorage::write(int slot, Value value) {
  ensureCapacity(slot);
  values_[slot] = value;
}

template <typename Object>
static Object *allocateObject(Vm &vm, ObjectKind type) {
  void *storage = allocate<Object>(vm);
  Object *object = new (storage) Object();
  Obj *header = static_cast<Obj *>(object);
  header->type = type;
  header->isMarked = false;

  header->next = vm.heap.objects();
  vm.heap.objects() = header;

#ifdef DEBUG_LOG_GC
  std::printf("%p allocate %zu for %d\n", (void *)object, sizeof(Object),
         objectKindIndex(type));
#endif

  return object;
}
ObjBoundMethod *Vm::newBoundMethod(Value receiver, ObjClosure *method) {
  ObjBoundMethod *bound =
      allocateObject<ObjBoundMethod>(*this, OBJ_BOUND_METHOD);
  bound->receiver = receiver;
  bound->method = method;
  return bound;
}
ObjClass *Vm::newClass(ObjString *name) {
  ObjClass *klass = allocateObject<ObjClass>(*this, OBJ_CLASS);
  klass->name = name;
  klass->initializer = nullptr;
  klass->fieldSlotCount = 0;
  klass->fieldVersion = 0;
  return klass;
}
ObjClosure *Vm::newClosure(ObjFunction *function) {
  ObjUpvalue **upvalues = allocate<ObjUpvalue *>(*this, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = nullptr;
  }

  ObjClosure *closure = allocateObject<ObjClosure>(*this, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues.adopt(*this, upvalues, function->upvalueCount);
  return closure;
}
ObjFunction *Vm::newFunction() {
  ObjFunction *function = allocateObject<ObjFunction>(*this, OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = nullptr;
  return function;
}
ObjInstance *Vm::newInstance(ObjClass *klass) {
  ObjInstance *instance = allocateObject<ObjInstance>(*this, OBJ_INSTANCE);
  instance->klass = klass;
  instance->fields.initialize(*this);
  return instance;
}
ObjNative *Vm::newNative(NativeFn function) {
  ObjNative *native = allocateObject<ObjNative>(*this, OBJ_NATIVE);
  native->function = function;
  return native;
}

static ObjString *allocateString(Vm &vm, char *chars, int length,
                                 uint32_t hash) {
  ObjString *string = allocateObject<ObjString>(vm, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  vm.push(objectValue(string));
  vm.strings.set(string, nilValue());
  vm.pop();

  return string;
}
static uint32_t hashString(const char *key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}
ObjString *Vm::takeString(char *chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString *interned = strings.findString(chars, length, hash);
  if (interned != nullptr) {
    freeArray(*this, chars, length + 1);
    return interned;
  }

  return allocateString(*this, chars, length, hash);
}
ObjString *Vm::copyString(const char *chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString *interned = strings.findString(chars, length, hash);
  if (interned != nullptr)
    return interned;

  char *heapChars = allocate<char>(*this, length + 1);
  std::memcpy(heapChars, chars, length);
  heapChars[length] = '\0';

  return allocateString(*this, heapChars, length, hash);
}
ObjUpvalue *Vm::newUpvalue(Value *slot) {
  ObjUpvalue *upvalue = allocateObject<ObjUpvalue>(*this, OBJ_UPVALUE);
  upvalue->closed = nilValue();
  upvalue->location = slot;
  upvalue->next = nullptr;
  return upvalue;
}

static void printFunction(std::ostream &out, ObjFunction *function) {
  if (function->name == nullptr) {
    out << "<script>";
    return;
  }
  out << "<fn " << function->name->chars << '>';
}
void printObject(std::ostream &out, Value value) {
  switch (objectType(value)) {
  case OBJ_BOUND_METHOD:
    printFunction(out, asBoundMethod(value)->method->function);
    break;
  case OBJ_CLASS:
    out << asClass(value)->name->chars;
    break;
  case OBJ_CLOSURE:
    printFunction(out, asClosure(value)->function);
    break;
  case OBJ_FUNCTION:
    printFunction(out, asFunction(value));
    break;
  case OBJ_INSTANCE:
    out << asInstance(value)->klass->name->chars << " instance";
    break;
  case OBJ_NATIVE:
    out << "<native fn>";
    break;
  case OBJ_STRING:
    out << asCString(value);
    break;
  case OBJ_UPVALUE:
    out << "upvalue";
    break;
  }
}

void printObject(Value value) { printObject(std::cout, value); }

} // namespace cpplox
