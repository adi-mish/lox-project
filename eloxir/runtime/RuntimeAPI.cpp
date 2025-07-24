#include "RuntimeAPI.h"
#include <chrono>
#include <iostream>

using namespace eloxir;

uint64_t elx_print(uint64_t bits) {
  Value v = Value::fromBits(bits);
  switch (v.tag()) {
  case Tag::NUMBER:
    std::cout << v.asNum();
    break;
  case Tag::BOOL:
    std::cout << (v.asBool() ? "true" : "false");
    break;
  case Tag::NIL:
    std::cout << "nil";
    break;
  default:
    std::cout << "<obj>";
    break;
  }
  std::cout << '\n';
  return bits;
}

uint64_t elx_clock() {
  using namespace std::chrono;
  auto secs = duration<double>(system_clock::now().time_since_epoch()).count();
  return Value::number(secs).getBits();
}
