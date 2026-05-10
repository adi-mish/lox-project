#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"
#include "object.h"

namespace cpplox {

void *reallocate(void *pointer, size_t oldSize, size_t newSize);

inline int growCapacity(int capacity) { return capacity < 8 ? 8 : capacity * 2; }

template <typename T> T *allocate(int count = 1) {
  return static_cast<T *>(reallocate(NULL, 0, sizeof(T) * count));
}

template <typename T> void release(T *pointer) {
  reallocate(pointer, sizeof(T), 0);
}

template <typename T> T *growArray(T *pointer, int oldCount, int newCount) {
  return static_cast<T *>(
      reallocate(pointer, sizeof(T) * oldCount, sizeof(T) * newCount));
}

template <typename T> void freeArray(T *pointer, int oldCount) {
  reallocate(pointer, sizeof(T) * oldCount, 0);
}

void markObject(Obj *object);
void markValue(Value value);
void collectGarbage();
void freeObjects();

} // namespace cpplox

#endif
