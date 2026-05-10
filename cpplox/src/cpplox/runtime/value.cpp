#include <cstring>
#include <iostream>
#include <ostream>

#include "memory.h"
#include "object.h"
#include "value.h"

namespace cpplox {

void printValue(std::ostream &out, Value value) {
  if (isBool(value)) {
    out << (asBool(value) ? "true" : "false");
  } else if (isNil(value)) {
    out << "nil";
  } else if (isNumber(value)) {
    out << asNumber(value);
  } else if (isObj(value)) {
    printObject(out, value);
  } else if (isUninitialized(value)) {
    out << "<uninitialized>";
  }
}

void printValue(Value value) { printValue(std::cout, value); }

} // namespace cpplox
