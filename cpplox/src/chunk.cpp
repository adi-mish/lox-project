#include <stdlib.h>

#include "chunk.h"
#include "memory.h"
#include "vm.h"

void initChunk(Chunk *chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  chunk->globalCaches = NULL;
  chunk->globalCacheCapacity = 0;
  initValueArray(&chunk->constants);
}
void freeChunk(Chunk *chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  FREE_ARRAY(GlobalCache, chunk->globalCaches, chunk->globalCacheCapacity);
  freeValueArray(&chunk->constants);
  initChunk(chunk);
}

void writeChunk(Chunk *chunk, uint8_t byte, int line) {
  if (chunk->capacity < chunk->count + 1) {
    int oldCapacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCapacity);
    chunk->code =
        GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
    chunk->lines = GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
  }

  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}
int addConstant(Chunk *chunk, Value value) {
  if (IS_OBJ(value)) {
    for (int i = 0; i < chunk->constants.count; i++) {
      if (valuesEqual(chunk->constants.values[i], value))
        return i;
    }
  }

  push(value);
  int oldCapacity = chunk->constants.capacity;
  writeValueArray(&chunk->constants, value);
  if (chunk->constants.capacity != oldCapacity) {
    chunk->globalCaches =
        GROW_ARRAY(GlobalCache, chunk->globalCaches, chunk->globalCacheCapacity,
                   chunk->constants.capacity);
    for (int i = chunk->globalCacheCapacity; i < chunk->constants.capacity;
         i++) {
      chunk->globalCaches[i].key = NULL;
      chunk->globalCaches[i].entry = NULL;
      chunk->globalCaches[i].tableVersion = 0;
    }
    chunk->globalCacheCapacity = chunk->constants.capacity;
  }
  chunk->globalCaches[chunk->constants.count - 1].key = NULL;
  chunk->globalCaches[chunk->constants.count - 1].entry = NULL;
  chunk->globalCaches[chunk->constants.count - 1].tableVersion = 0;
  pop();
  return chunk->constants.count - 1;
}
