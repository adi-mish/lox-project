#include <cstdio>
#include <cstring>

#include "memory.h"
#include "object.h"
#include "value.h"

namespace cpplox {

void initValueArray(ValueArray *array) {
  array->clear();
}
void writeValueArray(ValueArray *array, Value value) {
  array->push_back(value);
}
void freeValueArray(ValueArray *array) {
  ValueArray empty;
  array->swap(empty);
}
void printValue(Value value) {
#ifdef NAN_BOXING
  if (isBool(value)) {
    std::printf(asBool(value) ? "true" : "false");
  } else if (isNil(value)) {
    std::printf("nil");
  } else if (isNumber(value)) {
    std::printf("%g", asNumber(value));
  } else if (isObj(value)) {
    printObject(value);
  }
#else

  switch (value.type) {
  case VAL_BOOL:
    std::printf(asBool(value) ? "true" : "false");
    break;
  case VAL_NIL:
    std::printf("nil");
    break;
  case VAL_NUMBER:
    std::printf("%g", asNumber(value));
    break;
  case VAL_OBJ:
    printObject(value);
    break;
  case VAL_UNINITIALIZED:
    std::printf("<uninitialized>");
    break;
  }
#endif
}

} // namespace cpplox
