#pragma once

#include "common.h"
#include "object.h"

namespace cpplox {

class Vm;

void *reallocate(Vm &vm, void *pointer, size_t oldSize, size_t newSize);

inline int growCapacity(int capacity) { return capacity < 8 ? 8 : capacity * 2; }

template <typename T> T *allocate(Vm &vm, int count = 1) {
  return static_cast<T *>(reallocate(vm, nullptr, 0, sizeof(T) * count));
}

template <typename T> void release(Vm &vm, T *pointer) {
  reallocate(vm, pointer, sizeof(T), 0);
}

template <typename T>
T *growArray(Vm &vm, T *pointer, int oldCount, int newCount) {
  return static_cast<T *>(
      reallocate(vm, pointer, sizeof(T) * oldCount, sizeof(T) * newCount));
}

template <typename T> void freeArray(Vm &vm, T *pointer, int oldCount) {
  reallocate(vm, pointer, sizeof(T) * oldCount, 0);
}

void markObject(Vm &vm, Obj *object);
void markValue(Vm &vm, Value value);
void collectGarbage(Vm &vm);
void freeObjects(Vm &vm);

} // namespace cpplox
