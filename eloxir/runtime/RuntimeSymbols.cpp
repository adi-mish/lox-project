#include "RuntimeSymbols.h"
#include "RuntimeAPI.h"

#include <cstdint>

namespace eloxir {
namespace {

template <typename Function>
uint64_t runtimeAddress(Function *function) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(function));
}

#define ELX_RUNTIME_FUNCTION(symbol, signature, flags)                       \
  RuntimeFunctionDescriptor {                                                \
    #symbol, RuntimeSignature::signature, runtimeAddress(&::symbol), flags    \
  }

const RuntimeFunctionDescriptor kRuntimeFunctions[] = {
    ELX_RUNTIME_FUNCTION(elx_print, Value_Value, RuntimeNoUnwind),
    ELX_RUNTIME_FUNCTION(elx_clock, Value_None, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_readLine, Value_None, RuntimeNoFlags),

    ELX_RUNTIME_FUNCTION(elx_allocate_string, Value_I8Ptr_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_intern_string, Value_I8Ptr_I32, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_debug_string_address, Value_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_free_object, Void_Value, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_concatenate_strings, Value_Value_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_strings_equal, I32_Value_Value, RuntimeReadOnly),
    ELX_RUNTIME_FUNCTION(elx_strings_equal_interned, I32_Value_Value,
                         RuntimeReadOnly),
    ELX_RUNTIME_FUNCTION(elx_value_is_string, I32_Value, RuntimeReadOnly),

    ELX_RUNTIME_FUNCTION(elx_allocate_function, Value_I8Ptr_I32_I8Ptr,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_function, Value_Value_ValuePtr_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_value, Value_Value_ValuePtr_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_property, Value_Value_Value_ValuePtr_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_prepare_property_call, I32_Value_Value_ValuePtr,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_prepare_property_call_cached,
                         I32_Value_Value_CallCachePtr_ValuePtr,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_prepared_property,
                         Value_I32_Value_Value_ValuePtr_I32, RuntimeNoFlags),

    ELX_RUNTIME_FUNCTION(elx_is_function, I32_Value, RuntimeReadOnly),
    ELX_RUNTIME_FUNCTION(elx_is_closure, I32_Value, RuntimeReadOnly),
    ELX_RUNTIME_FUNCTION(elx_is_native, I32_Value, RuntimeReadOnly),
    ELX_RUNTIME_FUNCTION(elx_is_class, I32_Value, RuntimeReadOnly),
    ELX_RUNTIME_FUNCTION(elx_is_bound_method, I32_Value, RuntimeReadOnly),
    ELX_RUNTIME_FUNCTION(elx_allocate_native, Value_I8Ptr_I32_I8Ptr,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_native, Value_Value_ValuePtr_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_function_fast,
                         Value_Value_ValuePtr_I32_I8Ptr_I32, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_closure_fast,
                         Value_Value_ValuePtr_I32_I8Ptr_I32, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_native_fast,
                         Value_Value_ValuePtr_I32_I8Ptr_I32, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_bound_method_fast,
                         Value_Value_ValuePtr_I32_Value_I8Ptr_I32_Value_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_class_fast,
                         Value_Value_ValuePtr_I32_Value_I8Ptr_I32_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_bound_method_matches, I32_Value_Value_Value,
                         RuntimeReadOnly),
    ELX_RUNTIME_FUNCTION(elx_call_cache_invalidate, Void_CallCachePtr,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_cache_update, Void_CallCachePtr_Value,
                         RuntimeNoFlags),

    ELX_RUNTIME_FUNCTION(elx_cache_stats_enabled, I32_None, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_cache_stats_reset, Void_None, RuntimeNoFlags),
#ifdef ELOXIR_ENABLE_CACHE_STATS
    ELX_RUNTIME_FUNCTION(elx_cache_stats_record_property_hit, Void_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_cache_stats_record_property_miss, Void_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_cache_stats_record_property_shape_transition,
                         Void_I32, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_cache_stats_record_call_hit, Void_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_cache_stats_record_call_miss, Void_None,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_cache_stats_record_call_transition, Void_I32_I32,
                         RuntimeNoFlags),
#endif

    ELX_RUNTIME_FUNCTION(elx_allocate_upvalue, Value_ValuePtr, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_allocate_upvalue_with_value, Value_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_allocate_closure, Value_Value_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_set_closure_upvalue, Void_Value_I32_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_get_upvalue_value, Value_Value, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_set_upvalue_value, Void_Value_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_close_upvalues, Void_ValuePtr, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_call_closure, Value_Value_ValuePtr_I32,
                         RuntimeNoFlags),

    ELX_RUNTIME_FUNCTION(elx_validate_superclass, Value_Value, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_allocate_class, Value_Value_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_class_add_method, Void_Value_Value_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_class_prepare_field_shape, Void_Value_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_class_find_method, Value_Value_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_instantiate_class, Value_Value, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_instantiate_known_class, Value_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_get_instance_class, Value_Value, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_get_instance_field, Value_Value_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_set_instance_field, Value_Value_Value_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_set_instance_field_slot,
                         Value_Value_Value_I32_Value, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_try_get_instance_field, I32_Value_Value_ValuePtr,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_try_get_instance_field_cached,
                         I32_Value_Value_ValuePtr_ValuePtr_ValuePtr,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_set_instance_field_cached,
                         Value_Value_Value_ValuePtr_ValuePtr, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_bind_method, Value_Value_Value, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_get_property_slow, Value_Value_Value_CachePtr_I32,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_set_property_slow,
                         Value_Value_Value_Value_CachePtr_I32, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_instance_shape_ptr, I8Ptr_Value, RuntimeReadOnly),
    ELX_RUNTIME_FUNCTION(elx_instance_field_values_ptr, ValuePtr_Value,
                         RuntimeReadOnly),
    ELX_RUNTIME_FUNCTION(elx_instance_field_presence_ptr, PresencePtr_Value,
                         RuntimeReadOnly),

    ELX_RUNTIME_FUNCTION(elx_cleanup_all_objects, Void_None, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_get_global_builtin, Value_I8Ptr, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_initialize_global_builtins, Void_None,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_set_global_variable, Void_I8Ptr_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_get_global_variable, Value_I8Ptr, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_has_global_variable, I32_I8Ptr, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_set_global_function, Void_I8Ptr_Value,
                         RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_get_global_function, Value_I8Ptr, RuntimeNoFlags),
    ELX_RUNTIME_FUNCTION(elx_has_global_function, I32_I8Ptr, RuntimeNoFlags),

    ELX_RUNTIME_FUNCTION(elx_runtime_error, Void_I8Ptr, RuntimeNoUnwind),
    ELX_RUNTIME_FUNCTION(elx_runtime_error_silent, Void_I8Ptr,
                         RuntimeNoUnwind),
    ELX_RUNTIME_FUNCTION(elx_emit_runtime_error, Void_None, RuntimeNoUnwind),
    ELX_RUNTIME_FUNCTION(elx_has_runtime_error, I32_None, RuntimeReadOnly),
    ELX_RUNTIME_FUNCTION(elx_clear_runtime_error, Void_None, RuntimeNoUnwind),
    ELX_RUNTIME_FUNCTION(elx_safe_divide, Value_Value_Value, RuntimeNoUnwind),
};

#undef ELX_RUNTIME_FUNCTION

} // namespace

const RuntimeFunctionDescriptor *runtimeFunctionDescriptors() {
  return kRuntimeFunctions;
}

size_t runtimeFunctionDescriptorCount() {
  return sizeof(kRuntimeFunctions) / sizeof(kRuntimeFunctions[0]);
}

} // namespace eloxir
