#ifndef clox_compiler_h
#define clox_compiler_h

#include "object.h"
#include "vm.h"

namespace cpplox {

ObjFunction *compile(const char *source);
void markCompilerRoots();

} // namespace cpplox

#endif
