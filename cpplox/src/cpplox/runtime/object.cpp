#include <cstdio>
#include <cstring>

#include <new>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

namespace cpplox {

template <typename Object>
static Object *allocateObject(Vm &vm, ObjectKind type) {
  Object *object = allocate<Object>();
  Obj *header = reinterpret_cast<Obj *>(object);
  header->type = type;
  header->isMarked = false;

  header->next = vm.objects;
  vm.objects = header;

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
  new (&klass->methods) Table();
  new (&klass->fieldSlots) Table();
  klass->name = name;
  klass->initializer = nullptr;
  klass->fieldSlotCount = 0;
  klass->fieldVersion = 0;
  return klass;
}
ObjClosure *Vm::newClosure(ObjFunction *function) {
  ObjUpvalue **upvalues = allocate<ObjUpvalue *>(function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = nullptr;
  }

  ObjClosure *closure = allocateObject<ObjClosure>(*this, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}
ObjFunction *Vm::newFunction() {
  ObjFunction *function = allocateObject<ObjFunction>(*this, OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = nullptr;
  initChunk(&function->chunk);
  return function;
}
ObjInstance *Vm::newInstance(ObjClass *klass) {
  ObjInstance *instance = allocateObject<ObjInstance>(*this, OBJ_INSTANCE);
  instance->klass = klass;
  instance->fields = nullptr;
  instance->fieldCapacity = 0;
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

  vm.push(OBJ_VAL(string));
  vm.strings.set(string, NIL_VAL);
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
    freeArray(chars, length + 1);
    return interned;
  }

  return allocateString(*this, chars, length, hash);
}
ObjString *Vm::copyString(const char *chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString *interned = strings.findString(chars, length, hash);
  if (interned != nullptr)
    return interned;

  char *heapChars = allocate<char>(length + 1);
  std::memcpy(heapChars, chars, length);
  heapChars[length] = '\0';

  return allocateString(*this, heapChars, length, hash);
}
ObjUpvalue *Vm::newUpvalue(Value *slot) {
  ObjUpvalue *upvalue = allocateObject<ObjUpvalue>(*this, OBJ_UPVALUE);
  upvalue->closed = NIL_VAL;
  upvalue->location = slot;
  upvalue->next = nullptr;
  return upvalue;
}

static void printFunction(ObjFunction *function) {
  if (function->name == nullptr) {
    std::printf("<script>");
    return;
  }
  std::printf("<fn %s>", function->name->chars);
}
void printObject(Value value) {
  switch (objectType(value)) {
  case OBJ_BOUND_METHOD:
    printFunction(asBoundMethod(value)->method->function);
    break;
  case OBJ_CLASS:
    std::printf("%s", asClass(value)->name->chars);
    break;
  case OBJ_CLOSURE:
    printFunction(asClosure(value)->function);
    break;
  case OBJ_FUNCTION:
    printFunction(asFunction(value));
    break;
  case OBJ_INSTANCE:
    std::printf("%s instance", asInstance(value)->klass->name->chars);
    break;
  case OBJ_NATIVE:
    std::printf("<native fn>");
    break;
  case OBJ_STRING:
    std::printf("%s", asCString(value));
    break;
  case OBJ_UPVALUE:
    std::printf("upvalue");
    break;
  }
}

} // namespace cpplox
