#pragma once

#include <vector>

#include "common.h"
#include "object.h"

namespace cpplox {

class Vm;

class Heap {
public:
  void initialize();
  void *reallocate(Vm &vm, void *pointer, size_t oldSize, size_t newSize);

  size_t bytesAllocated() const { return bytesAllocated_; }
  size_t nextGC() const { return nextGC_; }
  void setNextGC(size_t nextGC) { nextGC_ = nextGC; }
  Obj *&objects() { return objects_; }
  std::vector<Obj *> &grayStack() { return grayStack_; }

private:
  size_t bytesAllocated_ = 0;
  size_t nextGC_ = 1024 * 1024;
  Obj *objects_ = nullptr;
  std::vector<Obj *> grayStack_;
};

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
