#include <stdio.h>
#include <string.h>

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
  if (IS_BOOL(value)) {
    printf(AS_BOOL(value) ? "true" : "false");
  } else if (IS_NIL(value)) {
    printf("nil");
  } else if (IS_NUMBER(value)) {
    printf("%g", AS_NUMBER(value));
  } else if (IS_OBJ(value)) {
    printObject(value);
  }
#else

  switch (value.type) {
  case VAL_BOOL:
    printf(AS_BOOL(value) ? "true" : "false");
    break;
  case VAL_NIL:
    printf("nil");
    break;
  case VAL_NUMBER:
    printf("%g", AS_NUMBER(value));
    break;
  case VAL_OBJ:
    printObject(value);
    break;
  case VAL_UNINITIALIZED:
    printf("<uninitialized>");
    break;
  }
#endif
}

} // namespace cpplox
