//> Chunks of Bytecode chunk-c
#include <stdlib.h>

#include "chunk.h"
//> chunk-c-include-memory
#include "memory.h"
//< chunk-c-include-memory
//> Garbage Collection chunk-include-vm
#include "vm.h"
//< Garbage Collection chunk-include-vm

void initChunk(Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
//> chunk-null-lines
  chunk->lines = NULL;
//< chunk-null-lines
  chunk->globalCaches = NULL;
  chunk->globalCacheCapacity = 0;
//> chunk-init-constant-array
  initValueArray(&chunk->constants);
//< chunk-init-constant-array
}
//> free-chunk
void freeChunk(Chunk* chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
//> chunk-free-lines
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
//< chunk-free-lines
  FREE_ARRAY(GlobalCache, chunk->globalCaches, chunk->globalCacheCapacity);
//> chunk-free-constants
  freeValueArray(&chunk->constants);
//< chunk-free-constants
  initChunk(chunk);
}
//< free-chunk
/* Chunks of Bytecode write-chunk < Chunks of Bytecode write-chunk-with-line
void writeChunk(Chunk* chunk, uint8_t byte) {
*/
//> write-chunk
//> write-chunk-with-line
void writeChunk(Chunk* chunk, uint8_t byte, int line) {
//< write-chunk-with-line
  if (chunk->capacity < chunk->count + 1) {
    int oldCapacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCapacity);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code,
        oldCapacity, chunk->capacity);
//> write-chunk-line
    chunk->lines = GROW_ARRAY(int, chunk->lines,
        oldCapacity, chunk->capacity);
//< write-chunk-line
  }

  chunk->code[chunk->count] = byte;
//> chunk-write-line
  chunk->lines[chunk->count] = line;
//< chunk-write-line
  chunk->count++;
}
//< write-chunk
//> add-constant
int addConstant(Chunk* chunk, Value value) {
  if (IS_OBJ(value)) {
    for (int i = 0; i < chunk->constants.count; i++) {
      if (valuesEqual(chunk->constants.values[i], value)) return i;
    }
  }

//> Garbage Collection add-constant-push
  push(value);
//< Garbage Collection add-constant-push
  int oldCapacity = chunk->constants.capacity;
  writeValueArray(&chunk->constants, value);
  if (chunk->constants.capacity != oldCapacity) {
    chunk->globalCaches = GROW_ARRAY(GlobalCache, chunk->globalCaches,
        chunk->globalCacheCapacity, chunk->constants.capacity);
    for (int i = chunk->globalCacheCapacity;
         i < chunk->constants.capacity; i++) {
      chunk->globalCaches[i].key = NULL;
      chunk->globalCaches[i].entry = NULL;
      chunk->globalCaches[i].tableVersion = 0;
    }
    chunk->globalCacheCapacity = chunk->constants.capacity;
  }
  chunk->globalCaches[chunk->constants.count - 1].key = NULL;
  chunk->globalCaches[chunk->constants.count - 1].entry = NULL;
  chunk->globalCaches[chunk->constants.count - 1].tableVersion = 0;
//> Garbage Collection add-constant-pop
  pop();
//< Garbage Collection add-constant-pop
  return chunk->constants.count - 1;
}
//< add-constant
