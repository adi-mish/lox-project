#include <cstdlib>

#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include "debug.h"
#include <cstdio>
#endif

namespace cpplox {

inline constexpr size_t GC_HEAP_GROW_FACTOR = 2;

void *reallocate(Vm &vm, void *pointer, size_t oldSize, size_t newSize) {
  vm.bytesAllocated += newSize - oldSize;
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage(vm);
#endif

    if (vm.bytesAllocated > vm.nextGC) {
      collectGarbage(vm);
    }
  }

  if (newSize == 0) {
    std::free(pointer);
    return nullptr;
  }

  void *result = std::realloc(pointer, newSize);
  if (result == nullptr)
    std::exit(1);
  return result;
}
void markObject(Vm &vm, Obj *object) {
  if (object == nullptr)
    return;
  if (object->isMarked)
    return;

#ifdef DEBUG_LOG_GC
  std::printf("%p mark ", (void *)object);
  printValue(objectValue(object));
  std::printf("\n");
#endif

  object->isMarked = true;

  vm.grayStack.push_back(object);
}
void markValue(Vm &vm, Value value) {
  if (isObj(value))
    markObject(vm, asObj(value));
}
static void markArray(Vm &vm, const ValueArray &array) {
  for (Value value : array) {
    markValue(vm, value);
  }
}
static void markInlineCaches(Vm &vm, Chunk *chunk) {
  for (int i = 0; i < static_cast<int>(chunk->constants().size()); i++) {
    InlineCache *cache = &chunk->inlineCache(i);
    if ((cache->kind == CACHE_FIELD || cache->kind == CACHE_METHOD) &&
        cache->owner != nullptr) {
      markObject(vm, reinterpret_cast<Obj *>(cache->owner));
    }
    if (cache->kind == CACHE_METHOD) {
      markValue(vm, cache->value);
    }
  }
}
static void blackenObject(Vm &vm, Obj *object) {
#ifdef DEBUG_LOG_GC
  std::printf("%p blacken ", (void *)object);
  printValue(objectValue(object));
  std::printf("\n");
#endif

  switch (object->type) {
  case OBJ_BOUND_METHOD: {
    ObjBoundMethod *bound = (ObjBoundMethod *)object;
    markValue(vm, bound->receiver);
    markObject(vm, (Obj *)bound->method);
    break;
  }
  case OBJ_CLASS: {
    ObjClass *klass = (ObjClass *)object;
    markObject(vm, (Obj *)klass->name);
    klass->methods.mark(vm);
    markObject(vm, (Obj *)klass->initializer);
    klass->fieldSlots.mark(vm);
    break;
  }
  case OBJ_CLOSURE: {
    ObjClosure *closure = (ObjClosure *)object;
    markObject(vm, (Obj *)closure->function);
    for (int i = 0; i < closure->upvalueCount; i++) {
      markObject(vm, (Obj *)closure->upvalues[i]);
    }
    break;
  }
  case OBJ_FUNCTION: {
    ObjFunction *function = (ObjFunction *)object;
    markObject(vm, (Obj *)function->name);
    markArray(vm, function->chunk.constants());
    markInlineCaches(vm, &function->chunk);
    break;
  }
  case OBJ_INSTANCE: {
    ObjInstance *instance = (ObjInstance *)object;
    markObject(vm, (Obj *)instance->klass);
    for (int i = 0; i < instance->fieldCapacity; i++) {
      if (!isUninitialized(instance->fields[i])) {
        markValue(vm, instance->fields[i]);
      }
    }
    break;
  }
  case OBJ_UPVALUE:
    markValue(vm, ((ObjUpvalue *)object)->closed);
    break;
  case OBJ_NATIVE:
  case OBJ_STRING:
    break;
  }
}
static void freeObject(Vm &vm, Obj *object) {
#ifdef DEBUG_LOG_GC
  std::printf("%p free type %d\n", (void *)object, objectKindIndex(object->type));
#endif

  switch (object->type) {
  case OBJ_BOUND_METHOD:
    release(vm, reinterpret_cast<ObjBoundMethod *>(object));
    break;
  case OBJ_CLASS: {
    ObjClass *klass = (ObjClass *)object;
    klass->methods.~Table();
    klass->fieldSlots.~Table();
    release(vm, klass);
    break;
  }
  case OBJ_CLOSURE: {
    ObjClosure *closure = (ObjClosure *)object;
    freeArray(vm, closure->upvalues, closure->upvalueCount);
    release(vm, closure);
    break;
  }
  case OBJ_FUNCTION: {
    ObjFunction *function = (ObjFunction *)object;
    freeChunk(&function->chunk);
    release(vm, function);
    break;
  }
  case OBJ_INSTANCE: {
    ObjInstance *instance = (ObjInstance *)object;
    freeArray(vm, instance->fields, instance->fieldCapacity);
    release(vm, instance);
    break;
  }
  case OBJ_NATIVE:
    release(vm, reinterpret_cast<ObjNative *>(object));
    break;
  case OBJ_STRING: {
    ObjString *string = (ObjString *)object;
    freeArray(vm, string->chars, string->length + 1);
    release(vm, string);
    break;
  }
  case OBJ_UPVALUE:
    release(vm, reinterpret_cast<ObjUpvalue *>(object));
    break;
  }
}
static void markRoots(Vm &vm) {
  for (Value *slot = vm.stack.data(); slot < vm.stackTop; slot++) {
    markValue(vm, *slot);
  }

  for (int i = 0; i < vm.frameCount; i++) {
    markObject(vm, (Obj *)vm.frames[i].closure);
  }

  for (ObjUpvalue *upvalue = vm.openUpvalues; upvalue != nullptr;
       upvalue = upvalue->next) {
    markObject(vm, (Obj *)upvalue);
  }

  vm.globals.mark(vm);
  vm.markCompilerRoots();
  markObject(vm, (Obj *)vm.initString);
}
static void traceReferences(Vm &vm) {
  while (!vm.grayStack.empty()) {
    Obj *object = vm.grayStack.back();
    vm.grayStack.pop_back();
    blackenObject(vm, object);
  }
}
static void sweep(Vm &vm) {
  Obj *previous = nullptr;
  Obj *object = vm.objects;
  while (object != nullptr) {
    if (object->isMarked) {
      object->isMarked = false;
      previous = object;
      object = object->next;
    } else {
      Obj *unreached = object;
      object = object->next;
      if (previous != nullptr) {
        previous->next = object;
      } else {
        vm.objects = object;
      }

      freeObject(vm, unreached);
    }
  }
}
void collectGarbage(Vm &vm) {
#ifdef DEBUG_LOG_GC
  std::printf("-- gc begin\n");
  size_t before = vm.bytesAllocated;
#endif

  markRoots(vm);
  traceReferences(vm);
  vm.strings.removeWhite();
  sweep(vm);

  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  std::printf("-- gc end\n");
  std::printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif
}
void freeObjects(Vm &vm) {
  Obj *object = vm.objects;
  while (object != nullptr) {
    Obj *next = object->next;
    freeObject(vm, object);
    object = next;
  }
  vm.grayStack.clear();
}

} // namespace cpplox
