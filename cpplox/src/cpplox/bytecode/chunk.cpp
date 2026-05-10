#include "chunk.h"

namespace cpplox {

void Chunk::write(uint8_t byte, int line) {
  code_.push_back(byte);
  lines_.push_back(line);
}

void Chunk::truncate(int size) {
  code_.resize(size);
  lines_.resize(size);
}

int Chunk::addConstant(Value value) {
  if (isObj(value)) {
    for (int i = 0; i < static_cast<int>(constants_.size()); i++) {
      if (valuesEqual(constants_[i], value))
        return i;
    }
  }

  constants_.push_back(value);
  inlineCaches_.emplace_back();
  return static_cast<int>(constants_.size()) - 1;
}

void writeChunk(Chunk *chunk, uint8_t byte, int line) {
  chunk->write(byte, line);
}

int addConstant(Chunk *chunk, Value value) {
  return chunk->addConstant(value);
}

} // namespace cpplox
