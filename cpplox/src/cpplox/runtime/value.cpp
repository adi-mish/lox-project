#include <cstdio>
#include <cstring>

#include "memory.h"
#include "object.h"
#include "value.h"

namespace cpplox {

void printValue(Value value) {
  if (isBool(value)) {
    std::printf(asBool(value) ? "true" : "false");
  } else if (isNil(value)) {
    std::printf("nil");
  } else if (isNumber(value)) {
    std::printf("%g", asNumber(value));
  } else if (isObj(value)) {
    printObject(value);
  } else if (isUninitialized(value)) {
    std::printf("<uninitialized>");
  }
}

} // namespace cpplox
