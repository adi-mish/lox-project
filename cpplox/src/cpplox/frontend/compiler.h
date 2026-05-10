#pragma once

#include <string_view>

#include "object.h"
#include "vm.h"

namespace cpplox {

ObjFunction *compile(Vm &vm, std::string_view source);

} // namespace cpplox
