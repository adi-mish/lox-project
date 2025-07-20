#pragma once
#include "Value.h"
#include <string>
#include <vector>

extern "C" {
    // Called from generated IR
    eloxir::Value  elx_print(eloxir::Value v);
    eloxir::Value  elx_clock();             // seconds since epoch
}
