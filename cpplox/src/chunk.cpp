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

void Chunk::write(uint8_t byte, int line) {
  code_.push_back(byte);
  lines_.push_back(line);
}

void Chunk::truncate(int size) {
  code_.resize(size);
  lines_.resize(size);
}

int Chunk::addConstant(Value value) {
  if (IS_OBJ(value)) {
    for (int i = 0; i < static_cast<int>(constants_.size()); i++) {
      if (valuesEqual(constants_[i], value))
        return i;
    }
  }

  push(value);
  writeValueArray(&constants_, value);
  inlineCaches_.push_back(emptyInlineCache());
  pop();
  return static_cast<int>(constants_.size()) - 1;
}

void writeChunk(Chunk *chunk, uint8_t byte, int line) {
  chunk->write(byte, line);
}

int addConstant(Chunk *chunk, Value value) {
  return chunk->addConstant(value);
}
