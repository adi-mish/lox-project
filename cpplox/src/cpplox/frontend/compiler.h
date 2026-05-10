#pragma once

#include "object.h"
#include "vm.h"

namespace cpplox {

ObjFunction *compile(Vm &vm, const char *source);

} // namespace cpplox

