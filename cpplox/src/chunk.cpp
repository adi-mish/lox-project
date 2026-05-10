#include <stdlib.h>

#include "chunk.h"
#include "memory.h"
#include "vm.h"

void initChunk(Chunk *chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  chunk->inlineCaches = NULL;
  chunk->inlineCacheCapacity = 0;
  initValueArray(&chunk->constants);
}
void freeChunk(Chunk *chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  FREE_ARRAY(InlineCache, chunk->inlineCaches, chunk->inlineCacheCapacity);
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
    chunk->inlineCaches =
        GROW_ARRAY(InlineCache, chunk->inlineCaches, chunk->inlineCacheCapacity,
                   chunk->constants.capacity);
    for (int i = chunk->inlineCacheCapacity; i < chunk->constants.capacity;
         i++) {
      chunk->inlineCaches[i].kind = CACHE_EMPTY;
      chunk->inlineCaches[i].key = NULL;
      chunk->inlineCaches[i].entry = NULL;
      chunk->inlineCaches[i].tableVersion = 0;
      chunk->inlineCaches[i].owner = NULL;
      chunk->inlineCaches[i].secondaryOwner = NULL;
      chunk->inlineCaches[i].secondaryVersion = 0;
      chunk->inlineCaches[i].entryIndex = -1;
      chunk->inlineCaches[i].tableCapacity = 0;
      chunk->inlineCaches[i].value = NIL_VAL;
    }
    chunk->inlineCacheCapacity = chunk->constants.capacity;
  }
  chunk->inlineCaches[chunk->constants.count - 1].kind = CACHE_EMPTY;
  chunk->inlineCaches[chunk->constants.count - 1].key = NULL;
  chunk->inlineCaches[chunk->constants.count - 1].entry = NULL;
  chunk->inlineCaches[chunk->constants.count - 1].tableVersion = 0;
  chunk->inlineCaches[chunk->constants.count - 1].owner = NULL;
  chunk->inlineCaches[chunk->constants.count - 1].secondaryOwner = NULL;
  chunk->inlineCaches[chunk->constants.count - 1].secondaryVersion = 0;
  chunk->inlineCaches[chunk->constants.count - 1].entryIndex = -1;
  chunk->inlineCaches[chunk->constants.count - 1].tableCapacity = 0;
  chunk->inlineCaches[chunk->constants.count - 1].value = NIL_VAL;
  pop();
  return chunk->constants.count - 1;
}
