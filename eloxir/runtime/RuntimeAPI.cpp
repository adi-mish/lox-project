#include "RuntimeAPI.h"
#include <iostream>
#include <chrono>

using namespace eloxir;

Value elx_print(Value v) {
    switch (v.tag()) {
        case Tag::NUMBER: std::cout << v.asNum(); break;
        case Tag::BOOL:   std::cout << (v.asBool() ? "true":"false"); break;
        case Tag::NIL:    std::cout << "nil"; break;
        default:          std::cout << "<obj>"; break;
    }
    std::cout << '\n';
    return v;
}

Value elx_clock() {
    using namespace std::chrono;
    auto secs = duration<double>(system_clock::now().time_since_epoch()).count();
    return Value::number(secs);
}
