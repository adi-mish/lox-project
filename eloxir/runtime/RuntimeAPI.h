#pragma once
#include "Value.h"
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
// Called from generated IR - use uint64_t for C compatibility
uint64_t elx_print(uint64_t v);
uint64_t elx_clock(); // seconds since epoch
}
