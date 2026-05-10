#include <cstdlib>

#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include "debug.h"
#include <stdio.h>
#endif

namespace cpplox {

inline constexpr size_t GC_HEAP_GROW_FACTOR = 2;

void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
  Vm &vm = currentVm();
  vm.bytesAllocated += newSize - oldSize;
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif

    if (vm.bytesAllocated > vm.nextGC) {
      collectGarbage();
    }
  }

  if (newSize == 0) {
    std::free(pointer);
    return NULL;
  }

  void *result = std::realloc(pointer, newSize);
  if (result == NULL)
    std::exit(1);
  return result;
}
void markObject(Obj *object) {
  Vm &vm = currentVm();
  if (object == NULL)
    return;
  if (object->isMarked)
    return;

#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void *)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  object->isMarked = true;

  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = growCapacity(vm.grayCapacity);
    vm.grayStack =
        (Obj **)std::realloc(vm.grayStack, sizeof(Obj *) * vm.grayCapacity);

    if (vm.grayStack == NULL)
      std::exit(1);
  }

  vm.grayStack[vm.grayCount++] = object;
}
void markValue(Value value) {
  if (IS_OBJ(value))
    markObject(AS_OBJ(value));
}
static void markArray(const ValueArray &array) {
  for (Value value : array) {
    markValue(value);
  }
}
static void markInlineCaches(Chunk *chunk) {
  for (int i = 0; i < static_cast<int>(chunk->constants().size()); i++) {
    InlineCache *cache = &chunk->inlineCache(i);
    if ((cache->kind == CACHE_FIELD || cache->kind == CACHE_METHOD) &&
        cache->owner != NULL) {
      markObject((Obj *)cache->owner);
    }
    if (cache->kind == CACHE_METHOD) {
      markValue(cache->value);
    }
  }
}
static void blackenObject(Obj *object) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void *)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  switch (object->type) {
  case OBJ_BOUND_METHOD: {
    ObjBoundMethod *bound = (ObjBoundMethod *)object;
    markValue(bound->receiver);
    markObject((Obj *)bound->method);
    break;
  }
  case OBJ_CLASS: {
    ObjClass *klass = (ObjClass *)object;
    markObject((Obj *)klass->name);
    klass->methods.mark();
    markObject((Obj *)klass->initializer);
    klass->fieldSlots.mark();
    break;
  }
  case OBJ_CLOSURE: {
    ObjClosure *closure = (ObjClosure *)object;
    markObject((Obj *)closure->function);
    for (int i = 0; i < closure->upvalueCount; i++) {
      markObject((Obj *)closure->upvalues[i]);
    }
    break;
  }
  case OBJ_FUNCTION: {
    ObjFunction *function = (ObjFunction *)object;
    markObject((Obj *)function->name);
    markArray(function->chunk.constants());
    markInlineCaches(&function->chunk);
    break;
  }
  case OBJ_INSTANCE: {
    ObjInstance *instance = (ObjInstance *)object;
    markObject((Obj *)instance->klass);
    for (int i = 0; i < instance->fieldCapacity; i++) {
      if (!IS_UNINITIALIZED(instance->fields[i])) {
        markValue(instance->fields[i]);
      }
    }
    break;
  }
  case OBJ_UPVALUE:
    markValue(((ObjUpvalue *)object)->closed);
    break;
  case OBJ_NATIVE:
  case OBJ_STRING:
    break;
  }
}
static void freeObject(Obj *object) {
#ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void *)object, objectKindIndex(object->type));
#endif

  switch (object->type) {
  case OBJ_BOUND_METHOD:
    release(reinterpret_cast<ObjBoundMethod *>(object));
    break;
  case OBJ_CLASS: {
    ObjClass *klass = (ObjClass *)object;
    klass->methods.~Table();
    klass->fieldSlots.~Table();
    release(klass);
    break;
  }
  case OBJ_CLOSURE: {
    ObjClosure *closure = (ObjClosure *)object;
    freeArray(closure->upvalues, closure->upvalueCount);
    release(closure);
    break;
  }
  case OBJ_FUNCTION: {
    ObjFunction *function = (ObjFunction *)object;
    freeChunk(&function->chunk);
    release(function);
    break;
  }
  case OBJ_INSTANCE: {
    ObjInstance *instance = (ObjInstance *)object;
    freeArray(instance->fields, instance->fieldCapacity);
    release(instance);
    break;
  }
  case OBJ_NATIVE:
    release(reinterpret_cast<ObjNative *>(object));
    break;
  case OBJ_STRING: {
    ObjString *string = (ObjString *)object;
    freeArray(string->chars, string->length + 1);
    release(string);
    break;
  }
  case OBJ_UPVALUE:
    release(reinterpret_cast<ObjUpvalue *>(object));
    break;
  }
}
static void markRoots() {
  Vm &vm = currentVm();
  for (Value *slot = vm.stack.data(); slot < vm.stackTop; slot++) {
    markValue(*slot);
  }

  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj *)vm.frames[i].closure);
  }

  for (ObjUpvalue *upvalue = vm.openUpvalues; upvalue != NULL;
       upvalue = upvalue->next) {
    markObject((Obj *)upvalue);
  }

  vm.globals.mark();
  markCompilerRoots();
  markObject((Obj *)vm.initString);
}
static void traceReferences() {
  Vm &vm = currentVm();
  while (vm.grayCount > 0) {
    Obj *object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}
static void sweep() {
  Vm &vm = currentVm();
  Obj *previous = NULL;
  Obj *object = vm.objects;
  while (object != NULL) {
    if (object->isMarked) {
      object->isMarked = false;
      previous = object;
      object = object->next;
    } else {
      Obj *unreached = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm.objects = object;
      }

      freeObject(unreached);
    }
  }
}
void collectGarbage() {
  Vm &vm = currentVm();
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm.bytesAllocated;
#endif

  markRoots();
  traceReferences();
  vm.strings.removeWhite();
  sweep();

  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif
}
void freeObjects() {
  Vm &vm = currentVm();
  Obj *object = vm.objects;
  while (object != NULL) {
    Obj *next = object->next;
    freeObject(object);
    object = next;
  }

  std::free(vm.grayStack);
}

} // namespace cpplox
