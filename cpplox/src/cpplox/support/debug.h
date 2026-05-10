#pragma once

#include "chunk.h"

namespace cpplox {

void disassembleChunk(Chunk *chunk, const char *name);
int disassembleInstruction(Chunk *chunk, int offset);

} // namespace cpplox

