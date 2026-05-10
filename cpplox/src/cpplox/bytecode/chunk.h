#ifndef clox_chunk_h
#define clox_chunk_h

#include <vector>

#include "common.h"
#include "value.h"

namespace cpplox {

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
  OP_CONSTANT_0,
  OP_CONSTANT_1,
  OP_CONSTANT_2,
  OP_CONSTANT_3,
  OP_CONSTANT_4,
  OP_CONSTANT_5,
  OP_CONSTANT_6,
  OP_CONSTANT_7,
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

class Chunk {
public:
  int size() const { return static_cast<int>(code_.size()); }
  bool empty() const { return code_.empty(); }

  uint8_t *codeData() { return code_.data(); }
  const uint8_t *codeData() const { return code_.data(); }
  uint8_t byteAt(int offset) const { return code_[offset]; }
  uint8_t &byteAt(int offset) { return code_[offset]; }
  int lineAt(size_t offset) const { return lines_[offset]; }

  void write(uint8_t byte, int line);
  void truncate(int size);
  int addConstant(Value value);

  Value constantAt(int index) const { return constants_[index]; }
  Value *constantsData() { return constants_.data(); }
  const ValueArray &constants() const { return constants_; }
  ValueArray &constants() { return constants_; }

  InlineCache &inlineCache(int index) { return inlineCaches_[index]; }
  const std::vector<InlineCache> &inlineCaches() const { return inlineCaches_; }
  std::vector<InlineCache> &inlineCaches() { return inlineCaches_; }

private:
  std::vector<uint8_t> code_;
  std::vector<int> lines_;
  std::vector<InlineCache> inlineCaches_;
  ValueArray constants_;
};

void initChunk(Chunk *chunk);
void freeChunk(Chunk *chunk);

void writeChunk(Chunk *chunk, uint8_t byte, int line);
int addConstant(Chunk *chunk, Value value);

} // namespace cpplox

#endif
