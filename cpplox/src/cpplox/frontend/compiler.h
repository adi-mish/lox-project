#ifndef clox_compiler_h
#define clox_compiler_h

#include "object.h"
#include "vm.h"

namespace cpplox {

ObjFunction *compile(Vm &vm, const char *source);

} // namespace cpplox

#endif
