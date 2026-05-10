#include <cstdlib>

#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include "debug.h"
#include <cstdio>
#endif

namespace cpplox {

inline constexpr size_t GC_HEAP_GROW_FACTOR = 2;

void Heap::initialize() {
  bytesAllocated_ = 0;
  nextGC_ = 1024 * 1024;
  objects_ = nullptr;
  grayStack_.clear();
}

void *Heap::reallocate(Vm &vm, void *pointer, size_t oldSize, size_t newSize) {
  bytesAllocated_ += newSize - oldSize;
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage(vm);
#endif

    if (bytesAllocated_ > nextGC_) {
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

void *reallocate(Vm &vm, void *pointer, size_t oldSize, size_t newSize) {
  return vm.heap.reallocate(vm, pointer, oldSize, newSize);
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

  vm.heap.grayStack().push_back(object);
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
        cache->ownerClass != nullptr) {
      markObject(vm, &cache->ownerClass->obj);
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
    for (int i = 0; i < closure->upvalues.size(); i++) {
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
    for (int i = 0; i < instance->fields.capacity(); i++) {
      Value value = instance->fields.data()[i];
      if (!isUninitialized(value)) {
        markValue(vm, value);
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

template <typename Object> void destroyObject(Vm &vm, Object *object) {
  object->~Object();
  release(vm, object);
}

static void freeObject(Vm &vm, Obj *object) {
#ifdef DEBUG_LOG_GC
  std::printf("%p free type %d\n", (void *)object, objectKindIndex(object->type));
#endif

  switch (object->type) {
  case OBJ_BOUND_METHOD:
    destroyObject(vm, reinterpret_cast<ObjBoundMethod *>(object));
    break;
  case OBJ_CLASS: {
    ObjClass *klass = (ObjClass *)object;
    destroyObject(vm, klass);
    break;
  }
  case OBJ_CLOSURE: {
    ObjClosure *closure = (ObjClosure *)object;
    destroyObject(vm, closure);
    break;
  }
  case OBJ_FUNCTION: {
    ObjFunction *function = (ObjFunction *)object;
    destroyObject(vm, function);
    break;
  }
  case OBJ_INSTANCE: {
    ObjInstance *instance = (ObjInstance *)object;
    destroyObject(vm, instance);
    break;
  }
  case OBJ_NATIVE:
    destroyObject(vm, reinterpret_cast<ObjNative *>(object));
    break;
  case OBJ_STRING: {
    ObjString *string = (ObjString *)object;
    freeArray(vm, string->chars, string->length + 1);
    destroyObject(vm, string);
    break;
  }
  case OBJ_UPVALUE:
    destroyObject(vm, reinterpret_cast<ObjUpvalue *>(object));
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
  auto &grayStack = vm.heap.grayStack();
  while (!grayStack.empty()) {
    Obj *object = grayStack.back();
    grayStack.pop_back();
    blackenObject(vm, object);
  }
}
static void sweep(Vm &vm) {
  Obj *previous = nullptr;
  Obj *object = vm.heap.objects();
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
        vm.heap.objects() = object;
      }

      freeObject(vm, unreached);
    }
  }
}
void collectGarbage(Vm &vm) {
#ifdef DEBUG_LOG_GC
  std::printf("-- gc begin\n");
  size_t before = vm.heap.bytesAllocated();
#endif

  markRoots(vm);
  traceReferences(vm);
  vm.strings.removeWhite();
  sweep(vm);

  vm.heap.setNextGC(vm.heap.bytesAllocated() * GC_HEAP_GROW_FACTOR);

#ifdef DEBUG_LOG_GC
  std::printf("-- gc end\n");
  std::printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.heap.bytesAllocated(), before, vm.heap.bytesAllocated(),
         vm.heap.nextGC());
#endif
}
void freeObjects(Vm &vm) {
  Obj *object = vm.heap.objects();
  while (object != nullptr) {
    Obj *next = object->next;
    freeObject(vm, object);
    object = next;
  }
  vm.heap.objects() = nullptr;
  vm.heap.grayStack().clear();
}

} // namespace cpplox
