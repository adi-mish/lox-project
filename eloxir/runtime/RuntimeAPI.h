#pragma once
#include "Value.h"
#include <cstdint>
#include <string>
#include <vector>

namespace eloxir {

// Object header for heap-allocated objects
enum class ObjType { STRING, FUNCTION };

struct Obj {
  ObjType type;
};

struct ObjString {
  Obj obj;
  int length;
  char chars[]; // flexible array member
};

struct ObjFunction {
  Obj obj;
  int arity;
  const char *name;
  void *llvm_function; // pointer to compiled LLVM function
};

} // namespace eloxir

extern "C" {
// Called from generated IR - use uint64_t for C compatibility
uint64_t elx_print(uint64_t v);
uint64_t elx_clock();    // seconds since epoch
uint64_t elx_readLine(); // read a line from stdin

// String functions
uint64_t elx_allocate_string(const char *chars, int length);
uint64_t elx_intern_string(const char *chars,
                           int length); // Global string interning
uint64_t elx_debug_string_address(
    uint64_t str_bits); // Debug: print string object address
void elx_free_object(uint64_t obj_bits);
uint64_t elx_concatenate_strings(uint64_t a_bits, uint64_t b_bits);
int elx_strings_equal(uint64_t a_bits, uint64_t b_bits);

// Function functions
uint64_t elx_allocate_function(const char *name, int arity,
                               void *llvm_function);
uint64_t elx_call_function(uint64_t func_bits, uint64_t *args, int arg_count);

// Memory management
void elx_cleanup_all_objects(); // Clean up all tracked objects

// Global built-ins management
uint64_t elx_get_global_builtin(const char *name);
void elx_initialize_global_builtins();

// Global environment for cross-line persistence
void elx_set_global_variable(const char *name, uint64_t value);
uint64_t elx_get_global_variable(const char *name);
int elx_has_global_variable(const char *name);
void elx_set_global_function(const char *name, uint64_t func_obj);
uint64_t elx_get_global_function(const char *name);
int elx_has_global_function(const char *name);

// Error handling
void elx_runtime_error(const char *message);
int elx_has_runtime_error();
void elx_clear_runtime_error();

// Safe arithmetic operations
uint64_t elx_safe_divide(uint64_t a_bits, uint64_t b_bits);
}
