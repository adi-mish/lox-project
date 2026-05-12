#pragma once

#include <cstddef>
#include <cstdint>

namespace eloxir {

enum class RuntimeSignature : uint8_t {
  Void_None,
  Void_Value,
  Void_ValuePtr,
  Void_I8Ptr,
  Void_I32,
  Void_Value_I32,
  Void_I32_I32,
  Void_Value_Value,
  Void_Value_Value_Value,
  Void_I8Ptr_Value,
  Void_Value_I32_Value,
  Value_None,
  Value_Value,
  I8Ptr_Value,
  Value_I8Ptr,
  ValuePtr_Value,
  PresencePtr_Value,
  Value_ValuePtr,
  Value_Value_Value,
  Value_I8Ptr_I32,
  Value_Value_I32,
  Value_Value_Value_Value,
  Value_Value_ValuePtr_I32,
  Value_Value_Value_ValuePtr_I32,
  Value_Value_Value_I32_Value,
  Value_Value_Value_ValuePtr_ValuePtr,
  Value_Value_Value_CachePtr_I32,
  Value_Value_Value_Value_CachePtr_I32,
  Value_I8Ptr_I32_I8Ptr,
  Value_I32_Value_Value_ValuePtr_I32,
  Value_Value_ValuePtr_I32_I8Ptr_I32,
  Value_Value_ValuePtr_I32_Value_I8Ptr_I32_Value_I32,
  Value_Value_ValuePtr_I32_Value_I8Ptr_I32_I32,
  I32_None,
  I32_Value,
  I32_Value_Value,
  I32_I8Ptr,
  I32_Value_Value_Value,
  I32_Value_Value_ValuePtr,
  I32_Value_Value_CallCachePtr_ValuePtr,
  I32_Value_Value_ValuePtr_ValuePtr_ValuePtr,
  Void_CallCachePtr,
  Void_CallCachePtr_Value,
};

enum RuntimeFunctionFlags : uint32_t {
  RuntimeNoFlags = 0,
  RuntimeNoUnwind = 1u << 0u,
  RuntimeReadOnly = 1u << 1u,
};

struct RuntimeFunctionDescriptor {
  const char *name;
  RuntimeSignature signature;
  uint64_t address;
  uint32_t flags;
};

const RuntimeFunctionDescriptor *runtimeFunctionDescriptors();
size_t runtimeFunctionDescriptorCount();

} // namespace eloxir
