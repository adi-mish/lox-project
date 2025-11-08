#pragma once
#include "Value.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <llvm/ADT/DenseMap.h>

namespace eloxir {

// Object header for heap-allocated objects
enum class ObjType {
  STRING,
  FUNCTION,
  NATIVE,
  CLOSURE,
  UPVALUE,
  CLASS,
  INSTANCE,
  BOUND_METHOD,
  SHAPE
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

using NativeFn = uint64_t (*)(uint64_t *args, int arg_count);

struct ObjNative {
  Obj obj;
  NativeFn function;
  const char *name; // Optional; may be null or empty.
  int arity;
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

struct ObjShape {
  Obj obj;
  ObjShape *parent;
  std::vector<ObjString *> fieldOrder;
  llvm::DenseMap<ObjString *, size_t> slotCache;
  llvm::DenseMap<ObjString *, ObjShape *> transitions;

  size_t fieldCount() const { return fieldOrder.size(); }
};

struct ObjClass {
  Obj obj;
  ObjString *name;
  struct ObjClass *superclass;
  llvm::DenseMap<ObjString *, uint64_t> methods;
  llvm::DenseMap<ObjString *, size_t> fieldSlots;
  ObjShape *shape;
};

struct ObjInstance {
  Obj obj;
  ObjClass *klass;
  ObjShape *shape;
  uint64_t *fieldValues;
  uint8_t *fieldPresence;
  size_t fieldCapacity;
  ObjInstance *nextFree;
};

struct ObjBoundMethod {
  Obj obj;
  uint64_t receiver;
  uint64_t method;
};

inline constexpr uint32_t PROPERTY_CACHE_MAX_SIZE = 4;

struct PropertyCacheEntry {
  ObjShape *shape;
  uint32_t slot;
};

struct PropertyCache {
  uint32_t size;
  PropertyCacheEntry entries[PROPERTY_CACHE_MAX_SIZE];
};

enum class CallInlineCacheKind : int32_t {
  EMPTY = 0,
  FUNCTION = 1,
  CLOSURE = 2,
  NATIVE = 3,
  BOUND_METHOD = 4,
  CLASS = 5
};

struct CallInlineCache {
  uint64_t callee_bits;   // Last callee value observed at this site
  uint64_t guard0_bits;   // Kind specific guard (method/initializer bits)
  uint64_t guard1_bits;   // Additional guard (expected class/shape pointer)
  void *target_ptr;       // Specialized entry point for the cached callee
  int32_t expected_arity; // Number of user arguments expected (or < 0 to skip)
  int32_t kind;           // eloxir::CallInlineCacheKind discriminator
  int32_t flags;          // Optional flags for specialised fast paths
  int32_t padding;
};

inline constexpr int CALL_CACHE_FLAG_METHOD_IS_CLOSURE = 1 << 0;
inline constexpr int CALL_CACHE_FLAG_METHOD_IS_FUNCTION = 1 << 1;
inline constexpr int CALL_CACHE_FLAG_METHOD_IS_NATIVE = 1 << 2;
inline constexpr int CALL_CACHE_FLAG_CLASS_HAS_INITIALIZER = 1 << 3;

struct CacheStats;

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
int elx_is_native(uint64_t value_bits);
int elx_is_class(uint64_t value_bits);
uint64_t elx_allocate_native(const char *name, int arity,
                             eloxir::NativeFn function);
uint64_t elx_call_native(uint64_t native_bits, uint64_t *args, int arg_count);
uint64_t elx_call_function_fast(uint64_t func_bits, uint64_t *args,
                                int arg_count, void *function_ptr,
                                int expected_arity);
uint64_t elx_call_closure_fast(uint64_t closure_bits, uint64_t *args,
                               int arg_count, void *function_ptr,
                               int expected_arity);
uint64_t elx_call_native_fast(uint64_t native_bits, uint64_t *args,
                              int arg_count, void *function_ptr,
                              int expected_arity);
uint64_t elx_call_bound_method_fast(uint64_t bound_bits, uint64_t *args,
                                    int arg_count, uint64_t method_bits,
                                    void *function_ptr, int expected_arity,
                                    uint64_t expected_class_ptr,
                                    int flags);
uint64_t elx_call_class_fast(uint64_t class_bits, uint64_t *args,
                             int arg_count, uint64_t initializer_bits,
                             void *function_ptr, int expected_arity,
                             int flags);
int elx_is_bound_method(uint64_t value_bits);
int elx_bound_method_matches(uint64_t callee_bits, uint64_t method_bits,
                             uint64_t expected_class_ptr);
void elx_call_cache_invalidate(eloxir::CallInlineCache *cache);
void elx_call_cache_update(eloxir::CallInlineCache *cache,
                           uint64_t callee_bits);

int elx_cache_stats_enabled();
void elx_cache_stats_reset();
eloxir::CacheStats elx_cache_stats_snapshot();
void elx_cache_stats_dump();

#if defined(ELOXIR_ENABLE_CACHE_STATS)
void elx_cache_stats_record_property_hit(int is_set);
void elx_cache_stats_record_property_miss(int is_set);
void elx_cache_stats_record_property_shape_transition(int is_set);
void elx_cache_stats_record_call_hit(int kind);
void elx_cache_stats_record_call_miss();
void elx_cache_stats_record_call_transition(int previous_kind, int new_kind);
#endif

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
int elx_try_get_instance_field(uint64_t instance_bits, uint64_t name_bits,
                               uint64_t *out_value);
uint64_t elx_bind_method(uint64_t instance_bits, uint64_t method_bits);
uint64_t elx_get_property_slow(uint64_t instance_bits, uint64_t name_bits,
                               eloxir::PropertyCache *cache,
                               uint32_t capacity);
uint64_t elx_set_property_slow(uint64_t instance_bits, uint64_t name_bits,
                               uint64_t value_bits,
                               eloxir::PropertyCache *cache,
                               uint32_t capacity);
eloxir::ObjShape *elx_instance_shape_ptr(uint64_t instance_bits);
uint64_t *elx_instance_field_values_ptr(uint64_t instance_bits);
uint8_t *elx_instance_field_presence_ptr(uint64_t instance_bits);

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
