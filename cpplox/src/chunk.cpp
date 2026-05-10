#include <new>

#include "chunk.h"
#include "vm.h"

static InlineCache emptyInlineCache() {
  InlineCache cache;
  cache.kind = CACHE_EMPTY;
  cache.key = NULL;
  cache.entry = NULL;
  cache.tableVersion = 0;
  cache.owner = NULL;
  cache.secondaryOwner = NULL;
  cache.secondaryVersion = 0;
  cache.entryIndex = -1;
  cache.tableCapacity = 0;
  cache.value = NIL_VAL;
  return cache;
}

void initChunk(Chunk *chunk) {
  new (chunk) Chunk();
}
void freeChunk(Chunk *chunk) {
  chunk->~Chunk();
}

void writeChunk(Chunk *chunk, uint8_t byte, int line) {
  chunk->code.push_back(byte);
  chunk->lines.push_back(line);
}
int addConstant(Chunk *chunk, Value value) {
  if (IS_OBJ(value)) {
    for (int i = 0; i < static_cast<int>(chunk->constants.size()); i++) {
      if (valuesEqual(chunk->constants[i], value))
        return i;
    }
  }

  push(value);
  writeValueArray(&chunk->constants, value);
  chunk->inlineCaches.push_back(emptyInlineCache());
  pop();
  return static_cast<int>(chunk->constants.size()) - 1;
}
