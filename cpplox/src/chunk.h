#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"
struct Entry;

typedef enum {
  CACHE_EMPTY,
  CACHE_GLOBAL,
  CACHE_FIELD,
  CACHE_METHOD,
} InlineCacheKind;

typedef struct {
  InlineCacheKind kind;
  ObjString *key;
  Entry *entry;
  uint32_t tableVersion;
  void *owner;
  void *secondaryOwner;
  uint32_t secondaryVersion;
  int entryIndex;
  int tableCapacity;
  Value value;
} InlineCache;

typedef enum {
  OP_CONSTANT,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_GET_LOCAL,
  OP_GET_LOCAL_0,
  OP_GET_LOCAL_1,
  OP_GET_LOCAL_2,
  OP_GET_LOCAL_3,
  OP_GET_LOCAL_4,
  OP_GET_LOCAL_5,
  OP_GET_LOCAL_6,
  OP_GET_LOCAL_7,
  OP_SET_LOCAL,
  OP_SET_LOCAL_0,
  OP_SET_LOCAL_1,
  OP_SET_LOCAL_2,
  OP_SET_LOCAL_3,
  OP_SET_LOCAL_4,
  OP_SET_LOCAL_5,
  OP_SET_LOCAL_6,
  OP_SET_LOCAL_7,
  OP_GET_GLOBAL,
  OP_DEFINE_GLOBAL,
  OP_SET_GLOBAL,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  OP_GET_PROPERTY,
  OP_SET_PROPERTY,
  OP_GET_SUPER,
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NOT,
  OP_NEGATE,
  OP_PRINT,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_CALL,
  OP_INVOKE,
  OP_SUPER_INVOKE,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_RETURN,
  OP_CLASS,
  OP_INHERIT,
  OP_METHOD,
  OP_COUNT
} OpCode;

typedef struct {
  int count;
  int capacity;
  uint8_t *code;
  int *lines;
  InlineCache *inlineCaches;
  int inlineCacheCapacity;
  ValueArray constants;
} Chunk;

void initChunk(Chunk *chunk);
void freeChunk(Chunk *chunk);

void writeChunk(Chunk *chunk, uint8_t byte, int line);
int addConstant(Chunk *chunk, Value value);

#endif
