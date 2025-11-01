#pragma once
#include "Value.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eloxir {

// Object header for heap-allocated objects
enum class ObjType {
  STRING,
  FUNCTION,
  CLOSURE,
  UPVALUE,
  CLASS,
  INSTANCE,
  BOUND_METHOD,
  NATIVE
};

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

struct ObjUpvalue {
  Obj obj;
  uint64_t *location;      // Points to the actual variable
  uint64_t closed;         // Value when upvalue is closed
  struct ObjUpvalue *next; // For tracking open upvalues
};

struct ObjClosure {
  Obj obj;
  ObjFunction *function;
  ObjUpvalue **upvalues; // Array of captured upvalues
  int upvalue_count;
};

struct ObjClass {
  Obj obj;
  ObjString *name;
  struct ObjClass *superclass;
  std::unordered_map<std::string, uint64_t> methods;
};

struct ObjInstance {
  Obj obj;
  ObjClass *klass;
  std::unordered_map<std::string, uint64_t> fields;
};

struct ObjBoundMethod {
  Obj obj;
  uint64_t receiver;
  uint64_t method;
};

using NativeFn = uint64_t (*)(uint64_t *args, int arg_count);

struct ObjNative {
  Obj obj;
  NativeFn function;
  const char *name;
  int arity;
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
int elx_value_is_string(uint64_t value_bits);

// Function functions
uint64_t elx_allocate_function(const char *name, int arity,
                               void *llvm_function);
uint64_t elx_call_function(uint64_t func_bits, uint64_t *args, int arg_count);
uint64_t elx_call_value(uint64_t callee_bits, uint64_t *args, int arg_count);
int elx_is_function(uint64_t value_bits);
uint64_t elx_allocate_native(const char *name, int arity,
                             eloxir::NativeFn function);
uint64_t elx_call_native(uint64_t native_bits, uint64_t *args, int arg_count);

// Closure and upvalue functions
uint64_t elx_allocate_upvalue(uint64_t *slot);
uint64_t
elx_allocate_upvalue_with_value(uint64_t value); // NEW: immediate value capture
uint64_t elx_allocate_closure(uint64_t function_bits, int upvalue_count);
void elx_set_closure_upvalue(uint64_t closure_bits, int index,
                             uint64_t upvalue_bits);
uint64_t elx_get_upvalue_value(uint64_t upvalue_bits);
void elx_set_upvalue_value(uint64_t upvalue_bits, uint64_t value);
void elx_close_upvalues(uint64_t *last_local);
uint64_t elx_call_closure(uint64_t closure_bits, uint64_t *args, int arg_count);
int elx_is_closure(uint64_t value_bits);

// Class and instance helpers
uint64_t elx_validate_superclass(uint64_t superclass_bits);
uint64_t elx_allocate_class(uint64_t name_bits, uint64_t superclass_bits);
void elx_class_add_method(uint64_t class_bits, uint64_t name_bits,
                          uint64_t method_bits);
uint64_t elx_class_find_method(uint64_t class_bits, uint64_t name_bits);
uint64_t elx_instantiate_class(uint64_t class_bits);
uint64_t elx_get_instance_class(uint64_t instance_bits);
uint64_t elx_get_instance_field(uint64_t instance_bits, uint64_t name_bits);
uint64_t elx_set_instance_field(uint64_t instance_bits, uint64_t name_bits,
                                uint64_t value_bits);
uint64_t elx_bind_method(uint64_t instance_bits, uint64_t method_bits);

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
void elx_runtime_error_silent(const char *message);
void elx_emit_runtime_error();
int elx_has_runtime_error();
void elx_clear_runtime_error();

// Safe arithmetic operations
uint64_t elx_safe_divide(uint64_t a_bits, uint64_t b_bits);
}
