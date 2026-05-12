#include "RuntimeAPI.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace eloxir;

static std::vector<void *> allocated_objects;
static bool object_tracking_enabled = true;

static void trackObject(void *object) {
  if (object_tracking_enabled) {
    allocated_objects.push_back(object);
  }
}

static void untrackObject(void *object) {
  if (!object_tracking_enabled)
    return;

  auto it =
      std::find(allocated_objects.begin(), allocated_objects.end(), object);
  if (it == allocated_objects.end())
    return;
  *it = allocated_objects.back();
  allocated_objects.pop_back();
}

// Global built-ins registry
static std::unordered_map<std::string, uint64_t> global_builtins;
static bool global_builtins_initialized = false;

// Global string interning table
static std::unordered_map<std::string, uint64_t> global_interned_strings;

// Global environment for cross-line persistence
static std::unordered_map<std::string, uint64_t> global_variables;
static std::unordered_map<std::string, uint64_t> global_functions;

static constexpr int PROPERTY_CALL_FIELD = 1;
static constexpr int PROPERTY_CALL_METHOD = 2;

extern "C" {
int elx_runtime_error_flag = 0;
int elx_current_call_depth = 0;
}

namespace {

// Runtime error state
static std::string runtime_error_message;
struct FieldBuffer {
  uint64_t *values;
  uint8_t *initialized;
};

static std::unordered_map<size_t, std::vector<FieldBuffer>> field_buffer_pool;
static ObjInstance *instance_pool_head = nullptr;
static constexpr size_t INSTANCE_ARENA_CHUNK_SIZE = 16384;
static std::vector<std::unique_ptr<ObjInstance[]>> instance_arena_chunks;
static ObjInstance *instance_arena_cursor = nullptr;
static ObjInstance *instance_arena_end = nullptr;

static FieldBuffer acquireFieldBuffer(size_t slotCount) {
  if (slotCount == 0) {
    return {nullptr, nullptr};
  }

  auto &bucket = field_buffer_pool[slotCount];
  FieldBuffer buffer{nullptr, nullptr};
  if (!bucket.empty()) {
    buffer = bucket.back();
    bucket.pop_back();
  } else {
    buffer.values = new uint64_t[slotCount];
    buffer.initialized = new uint8_t[slotCount];
  }

  std::fill_n(buffer.values, slotCount, Value::nil().getBits());
  std::memset(buffer.initialized, 0, slotCount * sizeof(uint8_t));
  return buffer;
}

static void releaseFieldBuffer(size_t slotCount, FieldBuffer buffer) {
  if (slotCount == 0 || !buffer.values || !buffer.initialized) {
    return;
  }

  field_buffer_pool[slotCount].push_back(buffer);
}

static void ensureInstanceCapacity(ObjInstance *instance, size_t required,
                                   bool preserveExisting) {
  if (!instance)
    return;

  if (required == 0) {
    if (instance->fieldValues) {
      releaseFieldBuffer(instance->fieldCapacity,
                         {instance->fieldValues, instance->fieldInitialized});
      instance->fieldValues = nullptr;
      instance->fieldInitialized = nullptr;
      instance->fieldCapacity = 0;
    }
    return;
  }

  if (instance->fieldCapacity >= required)
    return;

  FieldBuffer buffer = acquireFieldBuffer(required);
  if (preserveExisting && instance->fieldValues && instance->fieldInitialized) {
    std::memcpy(buffer.values, instance->fieldValues,
                instance->fieldCapacity * sizeof(uint64_t));
    std::memcpy(buffer.initialized, instance->fieldInitialized,
                instance->fieldCapacity * sizeof(uint8_t));
  }

  if (instance->fieldValues || instance->fieldInitialized) {
    releaseFieldBuffer(instance->fieldCapacity,
                       {instance->fieldValues, instance->fieldInitialized});
  }

  instance->fieldValues = buffer.values;
  instance->fieldInitialized = buffer.initialized;
  instance->fieldCapacity = required;
}

static void resetInstanceFields(ObjInstance *instance, ObjShape *shape) {
  if (!instance)
    return;

  size_t slotCount = shape ? shape->slotCount : 0;
  ensureInstanceCapacity(instance, slotCount, false);
  size_t capacity = instance->fieldCapacity;
  if (capacity > 0) {
    std::fill_n(instance->fieldValues, capacity, Value::nil().getBits());
    std::memset(instance->fieldInitialized, 0, capacity * sizeof(uint8_t));
  }
  instance->shape = shape;
}

static ObjInstance *acquireInstanceObject() {
  ObjInstance *instance = nullptr;
  if (!object_tracking_enabled) {
    if (instance_arena_cursor == instance_arena_end) {
      auto chunk =
          std::unique_ptr<ObjInstance[]>(new ObjInstance[INSTANCE_ARENA_CHUNK_SIZE]);
      instance_arena_cursor = chunk.get();
      instance_arena_end = instance_arena_cursor + INSTANCE_ARENA_CHUNK_SIZE;
      instance_arena_chunks.push_back(std::move(chunk));
    }
    instance = instance_arena_cursor++;
    instance->fieldValues = nullptr;
    instance->fieldInitialized = nullptr;
    instance->fieldCapacity = 0;
    instance->nextFree = nullptr;
  } else if (instance_pool_head) {
    instance = instance_pool_head;
    instance_pool_head = instance->nextFree;
  } else {
    instance = new ObjInstance();
    instance->fieldValues = nullptr;
    instance->fieldInitialized = nullptr;
    instance->fieldCapacity = 0;
    instance->nextFree = nullptr;
  }

  instance->obj.type = ObjType::INSTANCE;
  instance->klass = nullptr;
  instance->shape = nullptr;
  instance->nextFree = nullptr;
  return instance;
}

static void releaseInstanceObject(ObjInstance *instance) {
  if (!instance)
    return;

  if (instance->fieldValues || instance->fieldInitialized) {
    releaseFieldBuffer(instance->fieldCapacity,
                       {instance->fieldValues, instance->fieldInitialized});
    instance->fieldValues = nullptr;
    instance->fieldInitialized = nullptr;
    instance->fieldCapacity = 0;
  }

  instance->klass = nullptr;
  instance->shape = nullptr;
  instance->nextFree = instance_pool_head;
  instance_pool_head = instance;
}

constexpr int MAX_CALL_DEPTH = 256;

constexpr uint64_t SUPERCLASS_VALIDATION_FAILED =
    std::numeric_limits<uint64_t>::max();

struct CallDepthGuard {
  CallDepthGuard() {
    if (elx_current_call_depth < MAX_CALL_DEPTH) {
      ++elx_current_call_depth;
      active = true;
    }
  }

  ~CallDepthGuard() {
    if (active) {
      --elx_current_call_depth;
    }
  }

  bool entered() const { return active; }

private:
  bool active = false;
};

static std::string formatArityError(const char *name, int expected, int got) {
  std::string display_name =
      (name && name[0] != '\0') ? name : std::string("<anonymous>");
  return "Expected " + std::to_string(expected) + " arguments but got " +
         std::to_string(got) + " for " + display_name + ".";
}

static std::string formatArityError(const ObjFunction *func, int got) {
  if (!func)
    return formatArityError("<anonymous>", 0, got);

  const char *name = func->name;
  return formatArityError(name, func->arity, got);
}

static uint64_t invoke_function_pointer(void *function_ptr, uint64_t *args,
                                        int arg_count) {
  if (!function_ptr) {
    elx_runtime_error("Function has no implementation.");
    return Value::nil().getBits();
  }

  try {
    switch (arg_count) {
    case 0: {
      using Fn = uint64_t (*)();
      return reinterpret_cast<Fn>(function_ptr)();
    }
    case 1: {
      using Fn = uint64_t (*)(uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(args[0]);
    }
    case 2: {
      using Fn = uint64_t (*)(uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(args[0], args[1]);
    }
    case 3: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(args[0], args[1], args[2]);
    }
    case 4: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(args[0], args[1], args[2],
                                                args[3]);
    }
    case 5: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(args[0], args[1], args[2],
                                                args[3], args[4]);
    }
    case 6: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(args[0], args[1], args[2],
                                                args[3], args[4], args[5]);
    }
    case 7: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
    }
    case 8: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(args[0], args[1], args[2],
                                                args[3], args[4], args[5],
                                                args[6], args[7]);
    }
    case 9: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(args[0], args[1], args[2],
                                                args[3], args[4], args[5],
                                                args[6], args[7], args[8]);
    }
    case 10: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9]);
    }
    case 11: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10]);
    }
    case 12: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10], args[11]);
    }
    case 13: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10], args[11], args[12]);
    }
    case 14: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10], args[11], args[12], args[13]);
    }
    case 15: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10], args[11], args[12], args[13],
          args[14]);
    }
    case 16: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10], args[11], args[12], args[13],
          args[14], args[15]);
    }
    default: {
      std::string error_msg =
          "Functions with " + std::to_string(arg_count) +
          " arguments are not yet supported. Maximum supported: 16.";
      elx_runtime_error(error_msg.c_str());
      return Value::nil().getBits();
    }
    }
  } catch (const std::exception &e) {
    std::string error_msg =
        "Exception during function call: " + std::string(e.what());
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  } catch (...) {
    elx_runtime_error("Unknown exception during function call.");
    return Value::nil().getBits();
  }
}

static uint64_t invoke_closure_pointer(void *function_ptr, uint64_t *args,
                                       int arg_count, uint64_t *upvalue_args) {
  std::unique_ptr<uint64_t, decltype(&free)> upvalues(upvalue_args, free);
  if (!function_ptr) {
    elx_runtime_error("Closure function has no implementation.");
    return Value::nil().getBits();
  }

  try {
    switch (arg_count) {
    case 0: {
      using Fn = uint64_t (*)(uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(upvalues.get());
    }
    case 1: {
      using Fn = uint64_t (*)(uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(args[0], upvalues.get());
    }
    case 2: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(args[0], args[1],
                                                upvalues.get());
    }
    case 3: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(args[0], args[1], args[2],
                                                upvalues.get());
    }
    case 4: {
      using Fn =
          uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(args[0], args[1], args[2],
                                                args[3], upvalues.get());
    }
    case 5: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], upvalues.get());
    }
    case 6: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], upvalues.get());
    }
    case 7: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(args[0], args[1], args[2],
                                                args[3], args[4], args[5],
                                                args[6], upvalues.get());
    }
    case 8: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], upvalues.get());
    }
    case 9: {
      using Fn =
          uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t, uint64_t, uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], upvalues.get());
    }
    case 10: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], upvalues.get());
    }
    case 11: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10], upvalues.get());
    }
    case 12: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10], args[11], upvalues.get());
    }
    case 13: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10], args[11], args[12],
          upvalues.get());
    }
    case 14: {
      using Fn =
          uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t, uint64_t, uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10], args[11], args[12], args[13],
          upvalues.get());
    }
    case 15: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10], args[11], args[12], args[13],
          args[14], upvalues.get());
    }
    case 16: {
      using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t *);
      return reinterpret_cast<Fn>(function_ptr)(
          args[0], args[1], args[2], args[3], args[4], args[5], args[6],
          args[7], args[8], args[9], args[10], args[11], args[12], args[13],
          args[14], args[15], upvalues.get());
    }
    default: {
      std::string error_msg =
          "Closures with " + std::to_string(arg_count) +
          " arguments are not yet supported. Maximum supported: 16.";
      elx_runtime_error(error_msg.c_str());
      return Value::nil().getBits();
    }
    }
  } catch (const std::exception &e) {
    std::string error_msg =
        "Exception during closure call: " + std::string(e.what());
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  } catch (...) {
    elx_runtime_error("Unknown exception during closure call.");
    return Value::nil().getBits();
  }
}

static bool shapeTryGetSlot(ObjShape *shape, ObjString *field,
                            size_t *slotOut) {
  if (!shape || !field)
    return false;

  auto it = shape->slotCache.find(field);
  if (it == shape->slotCache.end())
    return false;

  if (slotOut)
    *slotOut = it->second;
  return true;
}

} // namespace

static void propertyCacheUpdate(PropertyCache *cache, ObjShape *shape,
                                size_t slot, uint32_t capacity, bool is_set) {
#if !defined(ELOXIR_ENABLE_CACHE_STATS)
  (void)is_set;
#endif
  if (!cache || !shape || capacity == 0)
    return;

  capacity = std::min<uint32_t>(capacity, PROPERTY_CACHE_MAX_SIZE);
  if (capacity == 0)
    return;

  uint32_t boundedSlot = static_cast<uint32_t>(
      std::min<size_t>(slot, std::numeric_limits<uint32_t>::max()));

  uint32_t currentSize = std::min<uint32_t>(cache->size, capacity);
  for (uint32_t i = 0; i < currentSize; ++i) {
    PropertyCacheEntry &entry = cache->entries[i];
    if (entry.shape == shape) {
      entry.slot = boundedSlot;
      return;
    }
  }

  if (currentSize >= capacity)
    return;

  PropertyCacheEntry &entry = cache->entries[currentSize];
  entry.shape = shape;
  entry.slot = boundedSlot;
  cache->size = currentSize + 1;

#if defined(ELOXIR_ENABLE_CACHE_STATS)
  elx_cache_stats_record_property_shape_transition(is_set ? 1 : 0);
#endif
}

static bool propertyCacheLookup(PropertyCache *cache, ObjShape *shape,
                                uint32_t capacity, size_t *out_slot) {
  if (!cache || !shape || capacity == 0)
    return false;

  capacity = std::min<uint32_t>(capacity, PROPERTY_CACHE_MAX_SIZE);
  uint32_t currentSize = std::min<uint32_t>(cache->size, capacity);
  for (uint32_t i = 0; i < currentSize; ++i) {
    const PropertyCacheEntry &entry = cache->entries[i];
    if (entry.shape == shape) {
      if (out_slot)
        *out_slot = entry.slot;
      return true;
    }
  }
  return false;
}

static const char *getString(Value v) {
  if (!v.isObj())
    return nullptr;
  void *obj_ptr = v.asObj();
  if (obj_ptr == nullptr)
    return nullptr;

  ObjString *str = static_cast<ObjString *>(obj_ptr);
  if (str->obj.type != ObjType::STRING)
    return nullptr;
  return str->chars;
}

static ObjString *getStringObject(Value v) {
  if (!v.isObj())
    return nullptr;

  void *obj_ptr = v.asObj();
  if (obj_ptr == nullptr)
    return nullptr;

  ObjString *str = static_cast<ObjString *>(obj_ptr);
  if (str->obj.type != ObjType::STRING)
    return nullptr;
  return str;
}

static ObjFunction *getFunction(Value v) {
  if (!v.isObj())
    return nullptr;

  void *obj_ptr = v.asObj();
  if (obj_ptr == nullptr)
    return nullptr;

  ObjFunction *func = static_cast<ObjFunction *>(obj_ptr);
  if (func->obj.type != ObjType::FUNCTION)
    return nullptr;
  return func;
}

static ObjNative *getNative(Value v) {
  if (!v.isObj())
    return nullptr;

  void *obj_ptr = v.asObj();
  if (obj_ptr == nullptr)
    return nullptr;

  ObjNative *native = static_cast<ObjNative *>(obj_ptr);
  if (native->obj.type != ObjType::NATIVE)
    return nullptr;
  return native;
}

static ObjClosure *getClosure(Value v) {
  if (!v.isObj())
    return nullptr;

  void *obj_ptr = v.asObj();
  if (obj_ptr == nullptr)
    return nullptr;

  ObjClosure *closure = static_cast<ObjClosure *>(obj_ptr);
  if (closure->obj.type != ObjType::CLOSURE)
    return nullptr;
  return closure;
}

static ObjUpvalue *getUpvalue(Value v) {
  if (!v.isObj())
    return nullptr;

  void *obj_ptr = v.asObj();
  if (obj_ptr == nullptr)
    return nullptr;

  ObjUpvalue *upvalue = static_cast<ObjUpvalue *>(obj_ptr);
  if (upvalue->obj.type != ObjType::UPVALUE)
    return nullptr;
  return upvalue;
}

static ObjClass *getClass(Value v) {
  if (!v.isObj())
    return nullptr;

  void *obj_ptr = v.asObj();
  if (obj_ptr == nullptr)
    return nullptr;

  ObjClass *klass = static_cast<ObjClass *>(obj_ptr);
  if (klass->obj.type != ObjType::CLASS)
    return nullptr;
  return klass;
}

static ObjInstance *getInstance(Value v) {
  if (!v.isObj())
    return nullptr;

  void *obj_ptr = v.asObj();
  if (obj_ptr == nullptr)
    return nullptr;

  ObjInstance *instance = static_cast<ObjInstance *>(obj_ptr);
  if (instance->obj.type != ObjType::INSTANCE)
    return nullptr;
  return instance;
}

static ObjShape *loadCachedShape(uint64_t *cached_shape_bits) {
  if (!cached_shape_bits)
    return nullptr;
  uint64_t raw = *cached_shape_bits;
  if (raw == 0)
    return nullptr;
  return reinterpret_cast<ObjShape *>(raw);
}

static void storeCachedShape(uint64_t *cached_shape_bits, ObjShape *shape) {
  if (!cached_shape_bits)
    return;
  *cached_shape_bits = reinterpret_cast<uint64_t>(shape);
}

static size_t loadCachedSlot(uint64_t *cached_slot) {
  if (!cached_slot)
    return 0;
  return static_cast<size_t>(*cached_slot);
}

static void storeCachedSlot(uint64_t *cached_slot, size_t slot) {
  if (!cached_slot)
    return;
  *cached_slot = static_cast<uint64_t>(slot);
}

static ObjBoundMethod *getBoundMethod(Value v) {
  if (!v.isObj())
    return nullptr;

  void *obj_ptr = v.asObj();
  if (obj_ptr == nullptr)
    return nullptr;

  ObjBoundMethod *bound = static_cast<ObjBoundMethod *>(obj_ptr);
  if (bound->obj.type != ObjType::BOUND_METHOD)
    return nullptr;
  return bound;
}

static uint64_t findMethodOnClass(ObjClass *klass, ObjString *name);

static uint64_t callMethodWithoutBinding(uint64_t receiver_bits,
                                         uint64_t method_bits, uint64_t *args,
                                         int arg_count) {
  elx_clear_runtime_error();

  Value method_val = Value::fromBits(method_bits);
  ObjClosure *closure = getClosure(method_val);
  ObjFunction *func = closure ? closure->function : getFunction(method_val);
  ObjNative *native = (closure || func) ? nullptr : getNative(method_val);

  int expected_total = -1;
  const char *method_name = "<anonymous>";
  if (func) {
    expected_total = func->arity;
    if (func->name && func->name[0] != '\0') {
      method_name = func->name;
    }
  } else if (native) {
    expected_total = native->arity;
    if (native->name && native->name[0] != '\0') {
      method_name = native->name;
    }
  }

  if (expected_total >= 0) {
    int expected_user_args = expected_total > 0 ? expected_total - 1 : 0;
    if (arg_count != expected_user_args) {
      std::string error_msg =
          formatArityError(method_name, expected_user_args, arg_count);
      elx_runtime_error(error_msg.c_str());
      return Value::nil().getBits();
    }
  }

  size_t total_arg_count = static_cast<size_t>(arg_count) + 1;
  constexpr size_t kSmallArgCapacity = 16;
  uint64_t inline_arg = receiver_bits;
  std::array<uint64_t, kSmallArgCapacity> small_args;
  std::vector<uint64_t> heap_args;
  uint64_t *method_args = &inline_arg;

  if (arg_count != 0) {
    method_args = small_args.data();
    if (total_arg_count > small_args.size()) {
      heap_args.resize(total_arg_count);
      method_args = heap_args.data();
    }

    method_args[0] = receiver_bits;
    for (int i = 0; i < arg_count; ++i) {
      method_args[i + 1] = args ? args[i] : Value::nil().getBits();
    }
  }

  int call_arg_count = static_cast<int>(total_arg_count);

  if (func && call_arg_count > 255) {
    std::string error_msg = "Function arity (" +
                            std::to_string(call_arg_count) +
                            ") exceeds Lox limit of 255 parameters.";
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  CallDepthGuard depth_guard;
  if (!depth_guard.entered()) {
    elx_runtime_error("Stack overflow.");
    return Value::nil().getBits();
  }

  if (closure) {
    void *target = func ? func->llvm_function : nullptr;
    if (!target) {
      elx_runtime_error("Closure function has no implementation.");
      return Value::nil().getBits();
    }

    if (closure->upvalue_count == 0) {
      return invoke_function_pointer(target, method_args, call_arg_count);
    }

    uint64_t *upvalue_args = static_cast<uint64_t *>(
        malloc(sizeof(uint64_t) * closure->upvalue_count));
    if (!upvalue_args) {
      elx_runtime_error("Failed to allocate upvalue arguments.");
      return Value::nil().getBits();
    }

    for (int i = 0; i < closure->upvalue_count; i++) {
      if (closure->upvalues[i] != nullptr) {
        upvalue_args[i] = Value::object(closure->upvalues[i]).getBits();
      } else {
        upvalue_args[i] = Value::nil().getBits();
      }
    }

    return invoke_closure_pointer(target, method_args, call_arg_count,
                                  upvalue_args);
  }

  if (func) {
    if (!func->llvm_function) {
      elx_runtime_error("Function has no implementation.");
      return Value::nil().getBits();
    }

    return invoke_function_pointer(func->llvm_function, method_args,
                                   call_arg_count);
  }

  if (native) {
    if (!native->function) {
      elx_runtime_error("Can only call functions and classes.");
      return Value::nil().getBits();
    }

    try {
      return native->function(method_args, call_arg_count);
    } catch (const std::exception &e) {
      std::string error_msg =
          "Exception during native call: " + std::string(e.what());
      elx_runtime_error(error_msg.c_str());
      return Value::nil().getBits();
    } catch (...) {
      elx_runtime_error("Unknown exception during native call.");
      return Value::nil().getBits();
    }
  }

  return elx_call_value(method_bits, method_args, call_arg_count);
}

static ObjShape *ensureInstanceShape(ObjInstance *instance) {
  if (!instance)
    return nullptr;
  if (instance->shape)
    return instance->shape;
  if (instance->klass) {
    instance->shape = instance->klass->defaultShape;
  }
  return instance->shape;
}

static ObjString *extractStringKey(uint64_t string_bits, std::string *out) {
  Value string_val = Value::fromBits(string_bits);
  ObjString *str = getStringObject(string_val);
  if (!str)
    return nullptr;

  if (out) {
    out->assign(str->chars, str->length);
  }

  return str;
}

static void destroyObject(Obj *obj) {
  switch (obj->type) {
  case ObjType::CLASS:
    if (auto *klass = reinterpret_cast<ObjClass *>(obj)) {
      shapeDestroyTree(klass->rootShape);
      klass->rootShape = nullptr;
      klass->defaultShape = nullptr;
      delete klass;
    }
    break;
  case ObjType::INSTANCE:
    releaseInstanceObject(reinterpret_cast<ObjInstance *>(obj));
    break;
  case ObjType::BOUND_METHOD:
    delete reinterpret_cast<ObjBoundMethod *>(obj);
    break;
  case ObjType::NATIVE:
    free(obj);
    break;
  default:
    free(obj);
    break;
  }
}

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
  case Tag::OBJ: {
    void *obj_ptr = v.asObj();
    if (!obj_ptr) {
      std::cout << "<obj>";
      break;
    }

    Obj *obj = static_cast<Obj *>(obj_ptr);
    switch (obj->type) {
    case ObjType::STRING: {
      const char *str = getString(v);
      if (str) {
        std::cout << str;
      } else {
        std::cout << "<string>";
      }
      break;
    }
    case ObjType::FUNCTION: {
      ObjFunction *func = getFunction(v);
      if (func && func->name) {
        std::cout << "<fn " << func->name << ">";
      } else {
        std::cout << "<function>";
      }
      break;
    }
    case ObjType::NATIVE: {
      // Per the Lox specification, native functions print as "<native fn>"
      // regardless of any internal name they may carry.
      std::cout << "<native fn>";
      break;
    }
    case ObjType::CLOSURE: {
      ObjClosure *closure = getClosure(v);
      if (closure && closure->function && closure->function->name) {
        std::cout << "<closure " << closure->function->name << ">";
      } else {
        std::cout << "<closure>";
      }
      break;
    }
    case ObjType::UPVALUE: {
      std::cout << "<upvalue>";
      break;
    }
    case ObjType::CLASS: {
      ObjClass *klass = static_cast<ObjClass *>(obj_ptr);
      if (klass && klass->name) {
        std::cout << klass->name->chars;
      } else {
        std::cout << "<class>";
      }
      break;
    }
    case ObjType::INSTANCE: {
      ObjInstance *instance = static_cast<ObjInstance *>(obj_ptr);
      if (instance && instance->klass && instance->klass->name) {
        std::cout << instance->klass->name->chars << " instance";
      } else {
        std::cout << "<instance>";
      }
      break;
    }
    case ObjType::BOUND_METHOD: {
      ObjBoundMethod *bound = static_cast<ObjBoundMethod *>(obj_ptr);
      if (bound) {
        Value method_val = Value::fromBits(bound->method);
        ObjClosure *closure = getClosure(method_val);
        if (closure && closure->function && closure->function->name) {
          std::cout << "<fn " << closure->function->name << ">";
          break;
        }

        ObjFunction *func = getFunction(method_val);
        if (func && func->name) {
          std::cout << "<fn " << func->name << ">";
          break;
        }
      }
      std::cout << "<bound method>";
      break;
    }
    default:
      std::cout << "<obj>";
      break;
    }
    break;
  }
  default:
    std::cout << "<unknown>";
    break;
  }
  std::cout << '\n';
  return bits;
}

uint64_t elx_clock() {
  auto secs = std::chrono::duration<double>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
  return Value::number(secs).getBits();
}

uint64_t elx_readLine() {
  std::string line;
  if (std::getline(std::cin, line)) {
    return elx_intern_string(line.c_str(), line.length());
  } else {
    return Value::nil().getBits();
  }
}

uint64_t elx_debug_string_address(uint64_t str_bits) {
  Value v = Value::fromBits(str_bits);
  if (v.isObj()) {
    void *obj_ptr = v.asObj();
    ObjString *str = static_cast<ObjString *>(obj_ptr);
    if (str && str->obj.type == ObjType::STRING) {
      std::cout << "String \"" << str->chars << "\" at address: " << obj_ptr
                << std::endl;
    }
  }
  return str_bits; // Pass through the value
}

uint64_t elx_intern_string(const char *chars, int length) {
  auto [intern_it, inserted] =
      global_interned_strings.try_emplace(std::string(chars, length), 0);
  if (!inserted) {
    return intern_it->second;
  }

  uint64_t new_string = elx_allocate_string(intern_it->first.data(), length);
  if (Value::fromBits(new_string).isNil()) {
    global_interned_strings.erase(intern_it);
    return new_string;
  }

  intern_it->second = new_string;
  return new_string;
}

static ObjString *cachedInitName() {
  static uint64_t init_bits = 0;
  if (init_bits == 0) {
    init_bits = elx_intern_string("init", 4);
  }
  return getStringObject(Value::fromBits(init_bits));
}

uint64_t elx_allocate_string(const char *chars, int length) {
  // Allocate memory for the string object
  size_t size = offsetof(ObjString, chars) + length + 1;
  ObjString *str = static_cast<ObjString *>(malloc(size));
  if (!str) {
    return Value::nil().getBits();
  }

  str->obj.type = ObjType::STRING;
  str->length = length;
  std::memcpy(str->chars, chars, length);
  str->chars[length] = '\0'; // null terminate

  // Track the allocation
  trackObject(str);

  return Value::object(str).getBits();
}

void elx_free_object(uint64_t obj_bits) {
  Value v = Value::fromBits(obj_bits);
  if (!v.isObj())
    return;

  Obj *obj = static_cast<Obj *>(v.asObj());

  // Remove from tracking registry
  untrackObject(obj);

  destroyObject(obj);
}

uint64_t elx_concatenate_strings(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);

  if (!a.isObj() || !b.isObj()) {
    // Return nil if either operand is not an object
    return Value::nil().getBits();
  }

  ObjString *str_a = static_cast<ObjString *>(a.asObj());
  ObjString *str_b = static_cast<ObjString *>(b.asObj());

  if (str_a->obj.type != ObjType::STRING ||
      str_b->obj.type != ObjType::STRING) {
    return Value::nil().getBits();
  }

  int new_length = str_a->length + str_b->length;
  size_t size = offsetof(ObjString, chars) + new_length + 1;
  ObjString *result = static_cast<ObjString *>(malloc(size));

  result->obj.type = ObjType::STRING;
  result->length = new_length;

  std::memcpy(result->chars, str_a->chars, str_a->length);
  std::memcpy(result->chars + str_a->length, str_b->chars, str_b->length);
  result->chars[new_length] = '\0';

  // Track the allocation
  trackObject(result);

  return Value::object(result).getBits();
}

int elx_strings_equal(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);

  if (!a.isObj() || !b.isObj())
    return 0;

  ObjString *str_a = static_cast<ObjString *>(a.asObj());
  ObjString *str_b = static_cast<ObjString *>(b.asObj());

  if (str_a->obj.type != ObjType::STRING ||
      str_b->obj.type != ObjType::STRING) {
    return 0;
  }

  if (str_a->length != str_b->length)
    return 0;

  return std::memcmp(str_a->chars, str_b->chars, str_a->length) == 0 ? 1 : 0;
}

int elx_strings_equal_interned(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);

  ObjString *str_a = getStringObject(a);
  ObjString *str_b = getStringObject(b);
  if (!str_a || !str_b) {
    return elx_strings_equal(a_bits, b_bits);
  }

  return str_a == str_b ? 1 : 0;
}

int elx_value_is_string(uint64_t value_bits) {
  Value value = Value::fromBits(value_bits);
  ObjString *string_obj = getStringObject(value);
  return string_obj != nullptr ? 1 : 0;
}

int elx_is_function(uint64_t value_bits) {
  Value v = Value::fromBits(value_bits);
  ObjFunction *func = getFunction(v);
  return func ? 1 : 0;
}

uint64_t elx_allocate_function(const char *name, int arity,
                               void *llvm_function) {
  size_t name_len = 0;
  if (name) {
    const char *p = name;
    while (*p) {
      name_len++;
      p++;
    }
  }
  size_t size = sizeof(ObjFunction) + name_len + 1; // +1 for null terminator
  ObjFunction *func = static_cast<ObjFunction *>(malloc(size));
  if (!func) {
    return Value::nil().getBits();
  }

  func->obj.type = ObjType::FUNCTION;
  func->arity = arity;
  func->flags = 0;
  func->llvm_function = llvm_function;

  char *name_storage = reinterpret_cast<char *>(func + 1);
  if (name) {
    std::memcpy(name_storage, name, name_len);
  }
  name_storage[name_len] = '\0';
  func->name = name_storage;

  trackObject(func);

  return Value::object(func).getBits();
}

void elx_set_function_flags(uint64_t func_bits, int flags) {
  ObjFunction *func = getFunction(Value::fromBits(func_bits));
  if (!func) {
    return;
  }
  func->flags = static_cast<uint32_t>(flags);
}

uint64_t elx_call_function(uint64_t func_bits, uint64_t *args, int arg_count) {
  // Clear any previous runtime errors
  elx_clear_runtime_error();

  Value func_val = Value::fromBits(func_bits);
  ObjFunction *func = getFunction(func_val);
  if (!func) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  if (arg_count != func->arity) {
    std::string error_msg = formatArityError(func, arg_count);
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  // Validate argument count against Lox limit
  if (arg_count > 255) {
    std::string error_msg = "Function arity (" + std::to_string(arg_count) +
                            ") exceeds Lox limit of 255 parameters.";
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  if (!func->llvm_function) {
    elx_runtime_error("Function has no implementation.");
    return Value::nil().getBits();
  }

  CallDepthGuard depth_guard;
  if (!depth_guard.entered()) {
    elx_runtime_error("Stack overflow.");
    return Value::nil().getBits();
  }

  void *function_ptr = func->llvm_function;

  try {
    switch (arg_count) {
    case 0: {
      using FunctionPtr = uint64_t (*)();
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn();
    }
    case 1: {
      using FunctionPtr = uint64_t (*)(uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0]);
    }
    case 2: {
      using FunctionPtr = uint64_t (*)(uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1]);
    }
    case 3: {
      using FunctionPtr = uint64_t (*)(uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2]);
    }
    case 4: {
      using FunctionPtr = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3]);
    }
    case 5: {
      using FunctionPtr =
          uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4]);
    }
    case 6: {
      using FunctionPtr = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5]);
    }
    case 7: {
      using FunctionPtr = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
    }
    case 8: {
      using FunctionPtr = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7]);
    }
    case 9: {
      using FunctionPtr =
          uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8]);
    }
    case 10: {
      using FunctionPtr =
          uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9]);
    }
    case 11: {
      using FunctionPtr = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10]);
    }
    case 12: {
      using FunctionPtr = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10], args[11]);
    }
    case 13: {
      using FunctionPtr = uint64_t (*)(
          uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
          uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10], args[11], args[12]);
    }
    case 14: {
      using FunctionPtr = uint64_t (*)(
          uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
          uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10], args[11], args[12],
                args[13]);
    }
    case 15: {
      using FunctionPtr =
          uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                       uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10], args[11], args[12],
                args[13], args[14]);
    }
    case 16: {
      using FunctionPtr = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t);
      auto fn = reinterpret_cast<FunctionPtr>(function_ptr);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10], args[11], args[12],
                args[13], args[14], args[15]);
    }
    default: {
      std::string error_msg =
          "Functions with " + std::to_string(arg_count) +
          " arguments are not yet supported. Maximum supported: 16.";
      elx_runtime_error(error_msg.c_str());
      return Value::nil().getBits();
    }
    }
  } catch (const std::exception &e) {
    std::string error_msg =
        "Exception during function call: " + std::string(e.what());
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  } catch (...) {
    elx_runtime_error("Unknown exception during function call.");
    return Value::nil().getBits();
  }
}

uint64_t elx_allocate_native(const char *name, int arity, NativeFn function) {
  if (!function) {
    return Value::nil().getBits();
  }

  size_t name_len = 0;
  if (name) {
    const char *p = name;
    while (*p) {
      ++name_len;
      ++p;
    }
  }

  size_t size = sizeof(ObjNative) + name_len + 1;
  ObjNative *native = static_cast<ObjNative *>(malloc(size));
  if (!native) {
    return Value::nil().getBits();
  }

  native->obj.type = ObjType::NATIVE;
  native->function = function;
  native->arity = arity;

  char *name_storage = reinterpret_cast<char *>(native + 1);
  if (name && name_len > 0) {
    std::memcpy(name_storage, name, name_len);
    name_storage[name_len] = '\0';
    native->name = name_storage;
  } else {
    name_storage[0] = '\0';
    native->name = nullptr;
  }

  trackObject(native);
  return Value::object(native).getBits();
}

uint64_t elx_call_native(uint64_t native_bits, uint64_t *args, int arg_count) {
  elx_clear_runtime_error();

  Value native_val = Value::fromBits(native_bits);
  ObjNative *native = getNative(native_val);
  if (!native || !native->function) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  if (native->arity >= 0 && arg_count != native->arity) {
    const char *name = native->name ? native->name : "<native fn>";
    std::string error_msg = formatArityError(name, native->arity, arg_count);
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  CallDepthGuard depth_guard;
  if (!depth_guard.entered()) {
    elx_runtime_error("Stack overflow.");
    return Value::nil().getBits();
  }

  try {
    return native->function(args, arg_count);
  } catch (const std::exception &e) {
    std::string error_msg =
        "Exception during native call: " + std::string(e.what());
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  } catch (...) {
    elx_runtime_error("Unknown exception during native call.");
    return Value::nil().getBits();
  }
}

uint64_t elx_call_value(uint64_t callee_bits, uint64_t *args, int arg_count) {
  // Start each call without a pending runtime error.
  elx_clear_runtime_error();

  Value callee_val = Value::fromBits(callee_bits);
  if (!callee_val.isObj()) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  void *obj_ptr = callee_val.asObj();
  if (obj_ptr == nullptr) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  Obj *obj = static_cast<Obj *>(obj_ptr);

  switch (obj->type) {
  case ObjType::FUNCTION:
    return elx_call_function(callee_bits, args, arg_count);
  case ObjType::NATIVE:
    return elx_call_native(callee_bits, args, arg_count);
  case ObjType::CLOSURE:
    return elx_call_closure(callee_bits, args, arg_count);
  case ObjType::CLASS: {
    ObjClass *klass = static_cast<ObjClass *>(obj_ptr);

    uint64_t instance_bits = elx_instantiate_known_class(callee_bits);
    if (elx_has_runtime_error())
      return Value::nil().getBits();

    ObjString *init_name = cachedInitName();
    uint64_t initializer_bits = init_name ? findMethodOnClass(klass, init_name)
                                          : Value::nil().getBits();
    if (initializer_bits != Value::nil().getBits()) {
      Value initializer_val = Value::fromBits(initializer_bits);
      ObjClosure *init_closure = getClosure(initializer_val);
      ObjFunction *init_func =
          init_closure ? init_closure->function : getFunction(initializer_val);

      const char *init_name = "init";
      int expected_user_args = 0;
      if (init_func) {
        if (init_func->name && init_func->name[0] != '\0')
          init_name = init_func->name;
        int expected_total = init_func->arity;
        expected_user_args = expected_total > 0 ? expected_total - 1 : 0;
      }

      if (arg_count != expected_user_args) {
        std::string error_msg =
            formatArityError(init_name, expected_user_args, arg_count);
        elx_runtime_error(error_msg.c_str());
        return Value::nil().getBits();
      }

      callMethodWithoutBinding(instance_bits, initializer_bits, args,
                               arg_count);
      if (elx_has_runtime_error())
        return Value::nil().getBits();
    } else if (arg_count != 0) {
      const char *class_name =
          (klass->name && klass->name->chars && klass->name->length > 0)
              ? klass->name->chars
              : "<anonymous>";
      std::string error_msg = formatArityError(class_name, 0, arg_count);
      elx_runtime_error(error_msg.c_str());
      return Value::nil().getBits();
    }

    return instance_bits;
  }
  case ObjType::BOUND_METHOD: {
    ObjBoundMethod *bound = getBoundMethod(callee_val);
    if (!bound) {
      elx_runtime_error("Can only call functions and classes.");
      return Value::nil().getBits();
    }

    return callMethodWithoutBinding(bound->receiver, bound->method, args,
                                    arg_count);
  }
  default:
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }
}

// Global open upvalues list for proper closure behavior
static ObjUpvalue *open_upvalues = nullptr;

uint64_t elx_allocate_upvalue(uint64_t *slot) {
  // Check if we already have an upvalue for this slot
  ObjUpvalue *prev_upvalue = nullptr;
  ObjUpvalue *upvalue = open_upvalues;

  while (upvalue != nullptr && upvalue->location > slot) {
    prev_upvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != nullptr && upvalue->location == slot) {
    // Reuse existing upvalue
    return Value::object(upvalue).getBits();
  }

  // Create new upvalue
  ObjUpvalue *created_upvalue =
      static_cast<ObjUpvalue *>(malloc(sizeof(ObjUpvalue)));
  if (!created_upvalue) {
    return Value::nil().getBits();
  }

  created_upvalue->obj.type = ObjType::UPVALUE;
  created_upvalue->location = slot;
  created_upvalue->closed = 0;
  created_upvalue->next = upvalue;

  // Insert into open upvalues list
  if (prev_upvalue == nullptr) {
    open_upvalues = created_upvalue;
  } else {
    prev_upvalue->next = created_upvalue;
  }

  // Track the allocation
  trackObject(created_upvalue);

  return Value::object(created_upvalue).getBits();
}

uint64_t elx_allocate_upvalue_with_value(uint64_t value) {
  // Create new upvalue that immediately captures the given value
  ObjUpvalue *created_upvalue =
      static_cast<ObjUpvalue *>(malloc(sizeof(ObjUpvalue)));
  if (!created_upvalue) {
    return Value::nil().getBits();
  }

  created_upvalue->obj.type = ObjType::UPVALUE;
  created_upvalue->location = nullptr; // No reference to original storage
  created_upvalue->closed = value; // Immediately close with the captured value
  created_upvalue->next = nullptr; // Not part of the open upvalues list

  // Track the allocation
  trackObject(created_upvalue);

  return Value::object(created_upvalue).getBits();
}

uint64_t elx_allocate_closure(uint64_t function_bits, int upvalue_count) {
  Value func_val = Value::fromBits(function_bits);
  ObjFunction *function = getFunction(func_val);

  if (!function) {
    elx_runtime_error("Cannot create closure from non-function.");
    return Value::nil().getBits();
  }

  // Allocate closure object with space for upvalue array
  size_t size = sizeof(ObjClosure) + sizeof(ObjUpvalue *) * upvalue_count;
  ObjClosure *closure = static_cast<ObjClosure *>(malloc(size));
  if (!closure) {
    return Value::nil().getBits();
  }

  closure->obj.type = ObjType::CLOSURE;
  closure->function = function;
  closure->upvalue_count = upvalue_count;

  // Set upvalues array pointer to space after closure struct
  if (upvalue_count > 0) {
    closure->upvalues = reinterpret_cast<ObjUpvalue **>(closure + 1);
    // Initialize to nullptr
    for (int i = 0; i < upvalue_count; i++) {
      closure->upvalues[i] = nullptr;
    }
  } else {
    closure->upvalues = nullptr;
  }

  // Track the allocation
  trackObject(closure);

  return Value::object(closure).getBits();
}

void elx_set_closure_upvalue(uint64_t closure_bits, int index,
                             uint64_t upvalue_bits) {
  Value closure_val = Value::fromBits(closure_bits);
  ObjClosure *closure = getClosure(closure_val);

  if (!closure) {
    elx_runtime_error("Cannot set upvalue on non-closure.");
    return;
  }

  if (index < 0 || index >= closure->upvalue_count) {
    elx_runtime_error("Upvalue index out of bounds.");
    return;
  }

  Value upvalue_val = Value::fromBits(upvalue_bits);
  ObjUpvalue *upvalue = getUpvalue(upvalue_val);

  if (!upvalue) {
    elx_runtime_error("Cannot set non-upvalue as closure upvalue.");
    return;
  }

  closure->upvalues[index] = upvalue;
}

uint64_t elx_get_upvalue_value(uint64_t upvalue_bits) {
  Value upvalue_val = Value::fromBits(upvalue_bits);
  ObjUpvalue *upvalue = getUpvalue(upvalue_val);

  if (!upvalue) {
    elx_runtime_error("Cannot get value from non-upvalue.");
    return Value::nil().getBits();
  }

  if (upvalue->location != nullptr) {
    // Upvalue is still open, return current value
    return *(upvalue->location);
  } else {
    // Upvalue is closed, return stored value
    return upvalue->closed;
  }
}

void elx_set_upvalue_value(uint64_t upvalue_bits, uint64_t value) {
  Value upvalue_val = Value::fromBits(upvalue_bits);
  ObjUpvalue *upvalue = getUpvalue(upvalue_val);

  if (!upvalue) {
    elx_runtime_error("Cannot set value on non-upvalue.");
    return;
  }

  if (upvalue->location != nullptr) {
    // Upvalue is still open, set current value
    *(upvalue->location) = value;
  } else {
    // Upvalue is closed, set stored value
    upvalue->closed = value;
  }
}

void elx_close_upvalues(uint64_t *last_local) {
  while (open_upvalues != nullptr && open_upvalues->location >= last_local) {
    ObjUpvalue *upvalue = open_upvalues;
    open_upvalues = upvalue->next;
    if (upvalue->location != nullptr) {
      upvalue->closed = *(upvalue->location);
      upvalue->location = nullptr;
    }
    upvalue->next = nullptr;
  }
}

uint64_t elx_call_closure(uint64_t closure_bits, uint64_t *args,
                          int arg_count) {
  // Clear any previous runtime errors
  elx_clear_runtime_error();

  Value closure_val = Value::fromBits(closure_bits);
  ObjClosure *closure = getClosure(closure_val);

  if (!closure) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  ObjFunction *func = closure->function;
  if (!func) {
    elx_runtime_error("Closure has no function.");
    return Value::nil().getBits();
  }

  if (arg_count != func->arity) {
    std::string error_msg = formatArityError(func, arg_count);
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  // Validate argument count against Lox limit
  if (arg_count > 255) {
    std::string error_msg = "Function arity (" + std::to_string(arg_count) +
                            ") exceeds Lox limit of 255 parameters.";
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  void *target = func->llvm_function;
  if (!target) {
    elx_runtime_error("Closure function has no implementation.");
    return Value::nil().getBits();
  }

  CallDepthGuard depth_guard;
  if (!depth_guard.entered()) {
    elx_runtime_error("Stack overflow.");
    return Value::nil().getBits();
  }

  if (closure->upvalue_count == 0) {
    return invoke_function_pointer(target, args, arg_count);
  }

  uint64_t *upvalue_args = static_cast<uint64_t *>(
      malloc(sizeof(uint64_t) * closure->upvalue_count));
  if (!upvalue_args) {
    elx_runtime_error("Failed to allocate upvalue arguments.");
    return Value::nil().getBits();
  }

  for (int i = 0; i < closure->upvalue_count; i++) {
    if (closure->upvalues[i] != nullptr) {
      upvalue_args[i] = Value::object(closure->upvalues[i]).getBits();
    } else {
      upvalue_args[i] = Value::nil().getBits();
    }
  }

  return invoke_closure_pointer(target, args, arg_count, upvalue_args);
}

int elx_is_closure(uint64_t value_bits) {
  Value v = Value::fromBits(value_bits);
  ObjClosure *closure = getClosure(v);
  bool result = closure != nullptr;
  return result ? 1 : 0;
}

int elx_is_native(uint64_t value_bits) {
  Value v = Value::fromBits(value_bits);
  ObjNative *native = getNative(v);
  return native ? 1 : 0;
}

int elx_is_class(uint64_t value_bits) {
  Value v = Value::fromBits(value_bits);
  ObjClass *klass = getClass(v);
  return klass ? 1 : 0;
}

int elx_is_bound_method(uint64_t value_bits) {
  Value v = Value::fromBits(value_bits);
  ObjBoundMethod *bound = getBoundMethod(v);
  return bound ? 1 : 0;
}

int elx_bound_method_matches(uint64_t callee_bits, uint64_t method_bits,
                             uint64_t expected_class_ptr) {
  Value callee_val = Value::fromBits(callee_bits);
  ObjBoundMethod *bound = getBoundMethod(callee_val);
  if (!bound)
    return 0;

  if (bound->method != method_bits)
    return 0;

  if (expected_class_ptr == 0)
    return 1;

  Value receiver_val = Value::fromBits(bound->receiver);
  ObjInstance *instance = getInstance(receiver_val);
  if (!instance || !instance->klass)
    return 0;

  return reinterpret_cast<uint64_t>(instance->klass) == expected_class_ptr ? 1
                                                                           : 0;
}

void elx_call_cache_invalidate(CallInlineCache *cache) {
  if (!cache)
    return;

  cache->callee_bits = 0;
  cache->guard0_bits = 0;
  cache->guard1_bits = 0;
  cache->target_ptr = nullptr;
  cache->expected_arity = 0;
  cache->kind = static_cast<int32_t>(CallInlineCacheKind::EMPTY);
  cache->flags = 0;
  cache->padding = 0;
}

static bool configureMethodCallCache(CallInlineCache *cache,
                                     uint64_t method_bits, ObjClass *klass,
                                     uint64_t callee_bits = 0) {
  if (!cache || !klass)
    return false;

  Value method_val = Value::fromBits(method_bits);
  ObjClosure *closure = getClosure(method_val);
  ObjFunction *func = closure ? closure->function : getFunction(method_val);
  ObjNative *native = closure ? nullptr : getNative(method_val);

  void *target = nullptr;
  int flags = 0;
  int expected_total = 0;

  if (closure && closure->function && closure->function->llvm_function) {
    target = closure->function->llvm_function;
    expected_total = closure->function->arity;
    flags |= CALL_CACHE_FLAG_METHOD_IS_CLOSURE;
    if (closure->upvalue_count > 0) {
      flags |= CALL_CACHE_FLAG_CLOSURE_HAS_UPVALUES;
    }
  } else if (func && func->llvm_function) {
    target = func->llvm_function;
    expected_total = func->arity;
    flags |= CALL_CACHE_FLAG_METHOD_IS_FUNCTION;
  } else if (native && native->function) {
    target = reinterpret_cast<void *>(native->function);
    expected_total = native->arity;
    flags |= CALL_CACHE_FLAG_METHOD_IS_NATIVE;
  } else {
    return false;
  }

  if (func && (func->flags & FUNCTION_FLAG_LEAF) != 0) {
    flags |= CALL_CACHE_FLAG_TARGET_LEAF;
  }
  if (func && (func->flags & FUNCTION_FLAG_NO_RUNTIME_ERROR) != 0) {
    flags |= CALL_CACHE_FLAG_TARGET_NO_RUNTIME_ERROR;
  }
  elx_call_cache_invalidate(cache);
  cache->callee_bits = callee_bits;
  cache->guard0_bits = method_bits;
  cache->guard1_bits = reinterpret_cast<uint64_t>(klass);
  cache->target_ptr = target;
  cache->kind = static_cast<int32_t>(CallInlineCacheKind::BOUND_METHOD);
  cache->flags = flags;
  cache->expected_arity = (expected_total >= 0)
                              ? (expected_total > 0 ? expected_total - 1 : 0)
                              : expected_total;
  return true;
}

void elx_call_cache_update(CallInlineCache *cache, uint64_t callee_bits) {
  if (!cache)
    return;

#if defined(ELOXIR_ENABLE_CACHE_STATS)
  CallInlineCache previous = *cache;
#endif

  elx_call_cache_invalidate(cache);

  Value callee_val = Value::fromBits(callee_bits);
  if (!callee_val.isObj())
    return;

  void *obj_ptr = callee_val.asObj();
  if (!obj_ptr)
    return;

  Obj *obj = static_cast<Obj *>(obj_ptr);
  [[maybe_unused]] bool updated = false;

  switch (obj->type) {
  case ObjType::FUNCTION: {
    ObjFunction *func = static_cast<ObjFunction *>(obj_ptr);
    if (!func->llvm_function)
      return;

    cache->callee_bits = callee_bits;
    cache->kind = static_cast<int32_t>(CallInlineCacheKind::FUNCTION);
    cache->target_ptr = func->llvm_function;
    cache->expected_arity = func->arity;
    if ((func->flags & FUNCTION_FLAG_LEAF) != 0) {
      cache->flags |= CALL_CACHE_FLAG_TARGET_LEAF;
    }
    if ((func->flags & FUNCTION_FLAG_NO_RUNTIME_ERROR) != 0) {
      cache->flags |= CALL_CACHE_FLAG_TARGET_NO_RUNTIME_ERROR;
    }
    updated = true;
    break;
  }
  case ObjType::CLOSURE: {
    ObjClosure *closure = static_cast<ObjClosure *>(obj_ptr);
    if (!closure->function || !closure->function->llvm_function)
      return;

    cache->callee_bits = callee_bits;
    cache->kind = static_cast<int32_t>(CallInlineCacheKind::CLOSURE);
    cache->target_ptr = closure->function->llvm_function;
    cache->expected_arity = closure->function->arity;
    if ((closure->function->flags & FUNCTION_FLAG_LEAF) != 0) {
      cache->flags |= CALL_CACHE_FLAG_TARGET_LEAF;
    }
    if ((closure->function->flags & FUNCTION_FLAG_NO_RUNTIME_ERROR) != 0) {
      cache->flags |= CALL_CACHE_FLAG_TARGET_NO_RUNTIME_ERROR;
    }
    updated = true;
    break;
  }
  case ObjType::NATIVE: {
    ObjNative *native = static_cast<ObjNative *>(obj_ptr);
    if (!native->function)
      return;

    cache->callee_bits = callee_bits;
    cache->kind = static_cast<int32_t>(CallInlineCacheKind::NATIVE);
    cache->target_ptr = reinterpret_cast<void *>(native->function);
    cache->expected_arity = native->arity;
    updated = true;
    break;
  }
  case ObjType::BOUND_METHOD: {
    ObjBoundMethod *bound = static_cast<ObjBoundMethod *>(obj_ptr);
    Value receiver_val = Value::fromBits(bound->receiver);
    ObjInstance *instance = getInstance(receiver_val);
    if (!instance || !instance->klass)
      return;

    updated = configureMethodCallCache(cache, bound->method, instance->klass,
                                       callee_bits);
    break;
  }
  case ObjType::CLASS: {
    ObjClass *klass = static_cast<ObjClass *>(obj_ptr);
    cache->callee_bits = callee_bits;
    cache->kind = static_cast<int32_t>(CallInlineCacheKind::CLASS);
    cache->guard1_bits = reinterpret_cast<uint64_t>(klass);

    ObjString *init_name = cachedInitName();
    uint64_t initializer_bits = init_name ? findMethodOnClass(klass, init_name)
                                          : Value::nil().getBits();

    if (initializer_bits == Value::nil().getBits()) {
      cache->expected_arity = 0;
      return;
    }

    Value init_val = Value::fromBits(initializer_bits);
    ObjClosure *closure = getClosure(init_val);
    ObjFunction *func = closure ? closure->function : getFunction(init_val);
    ObjNative *native = closure ? nullptr : getNative(init_val);

    void *target = nullptr;
    int flags = CALL_CACHE_FLAG_CLASS_HAS_INITIALIZER;
    int expected_total = 0;

    if (closure && closure->function && closure->function->llvm_function) {
      target = closure->function->llvm_function;
      expected_total = closure->function->arity;
      flags |= CALL_CACHE_FLAG_METHOD_IS_CLOSURE;
      if (closure->upvalue_count > 0) {
        flags |= CALL_CACHE_FLAG_CLOSURE_HAS_UPVALUES;
      }
    } else if (func && func->llvm_function) {
      target = func->llvm_function;
      expected_total = func->arity;
      flags |= CALL_CACHE_FLAG_METHOD_IS_FUNCTION;
    } else if (native && native->function) {
      target = reinterpret_cast<void *>(native->function);
      expected_total = native->arity;
      flags |= CALL_CACHE_FLAG_METHOD_IS_NATIVE;
    } else {
      return;
    }

    if (func && (func->flags & FUNCTION_FLAG_LEAF) != 0) {
      flags |= CALL_CACHE_FLAG_TARGET_LEAF;
    }
    if (func && (func->flags & FUNCTION_FLAG_NO_RUNTIME_ERROR) != 0) {
      flags |= CALL_CACHE_FLAG_TARGET_NO_RUNTIME_ERROR;
    }
    cache->guard0_bits = initializer_bits;
    cache->target_ptr = target;
    cache->flags = flags;
    cache->expected_arity = (expected_total >= 0)
                                ? (expected_total > 0 ? expected_total - 1 : 0)
                                : expected_total;
    updated = true;
    break;
  }
  default:
    break;
  }

#if defined(ELOXIR_ENABLE_CACHE_STATS)
  if (updated) {
    auto previous_kind = static_cast<CallInlineCacheKind>(previous.kind);
    auto new_kind = static_cast<CallInlineCacheKind>(cache->kind);
    bool changed = previous_kind != new_kind;

    if (!changed) {
      switch (new_kind) {
      case CallInlineCacheKind::FUNCTION:
      case CallInlineCacheKind::CLOSURE:
      case CallInlineCacheKind::NATIVE:
      case CallInlineCacheKind::CLASS:
        changed = (previous.callee_bits != cache->callee_bits) ||
                  (previous.guard0_bits != cache->guard0_bits) ||
                  (previous.guard1_bits != cache->guard1_bits);
        break;
      case CallInlineCacheKind::BOUND_METHOD:
        changed = (previous.callee_bits != cache->callee_bits) ||
                  (previous.guard0_bits != cache->guard0_bits) ||
                  (previous.guard1_bits != cache->guard1_bits);
        break;
      case CallInlineCacheKind::EMPTY:
        changed = false;
        break;
      }
    }

    if (changed) {
      elx_cache_stats_record_call_transition(static_cast<int>(previous.kind),
                                             static_cast<int>(cache->kind));
    }
  }
#endif
}

uint64_t elx_call_function_fast(uint64_t func_bits, uint64_t *args,
                                int arg_count, void *function_ptr,
                                int expected_arity) {
  elx_clear_runtime_error();

  Value func_val = Value::fromBits(func_bits);
  ObjFunction *func = getFunction(func_val);
  if (!func) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  if (expected_arity >= 0 && arg_count != expected_arity) {
    std::string error_msg = formatArityError(func, arg_count);
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  if (arg_count > 255) {
    std::string error_msg = "Function arity (" + std::to_string(arg_count) +
                            ") exceeds Lox limit of 255 parameters.";
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  void *target = function_ptr ? function_ptr : func->llvm_function;
  if (!target) {
    elx_runtime_error("Function has no implementation.");
    return Value::nil().getBits();
  }

  CallDepthGuard depth_guard;
  if (!depth_guard.entered()) {
    elx_runtime_error("Stack overflow.");
    return Value::nil().getBits();
  }

  return invoke_function_pointer(target, args, arg_count);
}

uint64_t elx_call_closure_fast(uint64_t closure_bits, uint64_t *args,
                               int arg_count, void *function_ptr,
                               int expected_arity) {
  elx_clear_runtime_error();

  Value closure_val = Value::fromBits(closure_bits);
  ObjClosure *closure = getClosure(closure_val);
  if (!closure) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  ObjFunction *func = closure->function;
  if (!func) {
    elx_runtime_error("Closure has no function.");
    return Value::nil().getBits();
  }

  if (expected_arity >= 0 && arg_count != expected_arity) {
    std::string error_msg = formatArityError(func, arg_count);
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  if (arg_count > 255) {
    std::string error_msg = "Function arity (" + std::to_string(arg_count) +
                            ") exceeds Lox limit of 255 parameters.";
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  void *target =
      function_ptr ? function_ptr : (func ? func->llvm_function : nullptr);
  if (!target) {
    elx_runtime_error("Closure function has no implementation.");
    return Value::nil().getBits();
  }

  CallDepthGuard depth_guard;
  if (!depth_guard.entered()) {
    elx_runtime_error("Stack overflow.");
    return Value::nil().getBits();
  }

  if (closure->upvalue_count == 0) {
    return invoke_function_pointer(target, args, arg_count);
  }

  uint64_t *upvalue_args = static_cast<uint64_t *>(
      malloc(sizeof(uint64_t) * closure->upvalue_count));
  if (!upvalue_args) {
    elx_runtime_error("Failed to allocate upvalue arguments.");
    return Value::nil().getBits();
  }

  for (int i = 0; i < closure->upvalue_count; i++) {
    if (closure->upvalues[i] != nullptr) {
      upvalue_args[i] = Value::object(closure->upvalues[i]).getBits();
    } else {
      upvalue_args[i] = Value::nil().getBits();
    }
  }

  return invoke_closure_pointer(target, args, arg_count, upvalue_args);
}

uint64_t elx_call_native_fast(uint64_t native_bits, uint64_t *args,
                              int arg_count, void *function_ptr,
                              int expected_arity) {
  elx_clear_runtime_error();

  Value native_val = Value::fromBits(native_bits);
  ObjNative *native = getNative(native_val);
  if (!native) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  if (expected_arity >= 0 && arg_count != expected_arity) {
    const char *name = native->name ? native->name : "<native fn>";
    std::string error_msg = formatArityError(name, expected_arity, arg_count);
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  CallDepthGuard depth_guard;
  if (!depth_guard.entered()) {
    elx_runtime_error("Stack overflow.");
    return Value::nil().getBits();
  }

  NativeFn target = function_ptr ? reinterpret_cast<NativeFn>(function_ptr)
                                 : native->function;
  if (!target) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  try {
    return target(args, arg_count);
  } catch (const std::exception &e) {
    std::string error_msg =
        "Exception during native call: " + std::string(e.what());
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  } catch (...) {
    elx_runtime_error("Unknown exception during native call.");
    return Value::nil().getBits();
  }
}

uint64_t elx_call_method_fast(uint64_t receiver_bits, uint64_t *args,
                              int arg_count, uint64_t method_bits,
                              void *function_ptr, int expected_arity,
                              int flags) {
  elx_clear_runtime_error();

  constexpr int kCallableMethodFlags = CALL_CACHE_FLAG_METHOD_IS_CLOSURE |
                                       CALL_CACHE_FLAG_METHOD_IS_FUNCTION |
                                       CALL_CACHE_FLAG_METHOD_IS_NATIVE;
  if (!function_ptr || (flags & kCallableMethodFlags) == 0) {
    return callMethodWithoutBinding(receiver_bits, method_bits, args,
                                    arg_count);
  }

  if (expected_arity >= 0 && arg_count != expected_arity) {
    Value method_val = Value::fromBits(method_bits);
    ObjClosure *closure = getClosure(method_val);
    ObjFunction *func = closure ? closure->function : getFunction(method_val);
    ObjNative *native = (closure || func) ? nullptr : getNative(method_val);
    const char *name = "<anonymous>";
    if (func && func->name && func->name[0] != '\0') {
      name = func->name;
    } else if (native && native->name && native->name[0] != '\0') {
      name = native->name;
    }

    std::string error_msg = formatArityError(name, expected_arity, arg_count);
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  size_t total_arg_count = static_cast<size_t>(arg_count) + 1;
  constexpr size_t kSmallArgCapacity = 16;
  uint64_t inline_arg = receiver_bits;
  std::array<uint64_t, kSmallArgCapacity> small_args;
  std::vector<uint64_t> heap_args;
  uint64_t *method_args = &inline_arg;

  if (arg_count != 0) {
    method_args = small_args.data();
    if (total_arg_count > small_args.size()) {
      heap_args.resize(total_arg_count);
      method_args = heap_args.data();
    }

    method_args[0] = receiver_bits;
    for (int i = 0; i < arg_count; ++i) {
      method_args[i + 1] = args ? args[i] : Value::nil().getBits();
    }
  }

  int method_arg_count = static_cast<int>(total_arg_count);
  int total_expected = expected_arity;
  if (total_expected >= 0) {
    total_expected += 1;
  }

  if ((flags & (CALL_CACHE_FLAG_METHOD_IS_CLOSURE |
                CALL_CACHE_FLAG_METHOD_IS_FUNCTION)) &&
      !(flags & CALL_CACHE_FLAG_CLOSURE_HAS_UPVALUES)) {
    if (method_arg_count > 255) {
      std::string error_msg =
          "Function arity (" + std::to_string(method_arg_count) +
          ") exceeds Lox limit of 255 parameters.";
      elx_runtime_error(error_msg.c_str());
      return Value::nil().getBits();
    }

    if ((flags & CALL_CACHE_FLAG_TARGET_LEAF) == 0) {
      CallDepthGuard depth_guard;
      if (!depth_guard.entered()) {
        elx_runtime_error("Stack overflow.");
        return Value::nil().getBits();
      }
      return invoke_function_pointer(function_ptr, method_args,
                                     method_arg_count);
    }

    return invoke_function_pointer(function_ptr, method_args,
                                   method_arg_count);
  }

  if (flags & CALL_CACHE_FLAG_METHOD_IS_NATIVE) {
    CallDepthGuard depth_guard;
    if (!depth_guard.entered()) {
      elx_runtime_error("Stack overflow.");
      return Value::nil().getBits();
    }

    NativeFn target = reinterpret_cast<NativeFn>(function_ptr);
    try {
      return target(method_args, method_arg_count);
    } catch (const std::exception &e) {
      std::string error_msg =
          "Exception during native call: " + std::string(e.what());
      elx_runtime_error(error_msg.c_str());
      return Value::nil().getBits();
    } catch (...) {
      elx_runtime_error("Unknown exception during native call.");
      return Value::nil().getBits();
    }
  }

  if (flags & CALL_CACHE_FLAG_METHOD_IS_CLOSURE) {
    return elx_call_closure_fast(method_bits, method_args, method_arg_count,
                                 function_ptr, total_expected);
  }

  if (flags & CALL_CACHE_FLAG_METHOD_IS_FUNCTION) {
    return elx_call_function_fast(method_bits, method_args, method_arg_count,
                                  function_ptr, total_expected);
  }

  if (flags & CALL_CACHE_FLAG_METHOD_IS_NATIVE) {
    return elx_call_native_fast(method_bits, method_args, method_arg_count,
                                function_ptr, total_expected);
  }

  return callMethodWithoutBinding(receiver_bits, method_bits, args, arg_count);
}

uint64_t elx_call_bound_method_fast(uint64_t bound_bits, uint64_t *args,
                                    int arg_count, uint64_t method_bits,
                                    void *function_ptr, int expected_arity,
                                    uint64_t expected_class_ptr, int flags) {
  elx_clear_runtime_error();

  Value bound_val = Value::fromBits(bound_bits);
  ObjBoundMethod *bound = getBoundMethod(bound_val);
  if (!bound) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  if (!elx_bound_method_matches(bound_bits, method_bits, expected_class_ptr)) {
    return elx_call_value(bound_bits, args, arg_count);
  }

  return elx_call_method_fast(bound->receiver, args, arg_count, method_bits,
                              function_ptr, expected_arity, flags);
}

uint64_t elx_call_class_fast(uint64_t class_bits, uint64_t *args, int arg_count,
                             uint64_t initializer_bits, void *function_ptr,
                             int expected_arity, int flags) {
  elx_clear_runtime_error();

  Value class_val = Value::fromBits(class_bits);
  ObjClass *klass = getClass(class_val);
  if (!klass) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  uint64_t instance_bits = elx_instantiate_class(class_bits);
  if (elx_has_runtime_error())
    return Value::nil().getBits();

  bool has_initializer = (flags & CALL_CACHE_FLAG_CLASS_HAS_INITIALIZER) != 0;
  if (!has_initializer) {
    if (arg_count != 0) {
      const char *class_name =
          (klass->name && klass->name->chars && klass->name->length > 0)
              ? klass->name->chars
              : "<anonymous>";
      std::string error_msg = formatArityError(class_name, 0, arg_count);
      elx_runtime_error(error_msg.c_str());
      return Value::nil().getBits();
    }
    return instance_bits;
  }

  if (expected_arity >= 0 && arg_count != expected_arity) {
    const char *class_name =
        (klass->name && klass->name->chars && klass->name->length > 0)
            ? klass->name->chars
            : "<anonymous>";
    std::string error_msg =
        formatArityError(class_name, expected_arity, arg_count);
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  }

  uint64_t result =
      elx_call_method_fast(instance_bits, args, arg_count, initializer_bits,
                           function_ptr, expected_arity, flags);

  if (elx_has_runtime_error())
    return Value::nil().getBits();

  (void)result;
  return instance_bits;
}

static uint64_t findMethodOnClass(ObjClass *klass, ObjString *name) {
  for (ObjClass *current = klass; current != nullptr;
       current = current->superclass) {
    auto it = current->methods.find(name);
    if (it != current->methods.end()) {
      return it->second;
    }
  }

  return Value::nil().getBits();
}

uint64_t elx_validate_superclass(uint64_t superclass_bits) {
  if (elx_has_runtime_error()) {
    return Value::nil().getBits();
  }

  Value superclass_val = Value::fromBits(superclass_bits);
  if (superclass_val.isNil()) {
    elx_runtime_error("Superclass must be a class.");
    return Value::nil().getBits();
  }

  ObjClass *superclass = getClass(superclass_val);
  if (!superclass) {
    elx_runtime_error("Superclass must be a class.");
    return SUPERCLASS_VALIDATION_FAILED;
  }

  return superclass_bits;
}

uint64_t elx_allocate_class(uint64_t name_bits, uint64_t superclass_bits) {
  if (elx_has_runtime_error()) {
    return Value::nil().getBits();
  }

  Value name_val = Value::fromBits(name_bits);
  ObjString *name_str = getStringObject(name_val);
  if (!name_str) {
    elx_runtime_error("Class name must be a string.");
    return Value::nil().getBits();
  }

  ObjClass *superclass = nullptr;
  Value superclass_val = Value::fromBits(superclass_bits);
  bool should_validate_superclass =
      !superclass_val.isNil() || elx_has_runtime_error();

  if (should_validate_superclass) {
    uint64_t validated_super_bits = elx_validate_superclass(superclass_bits);
    if (validated_super_bits == SUPERCLASS_VALIDATION_FAILED) {
      return Value::nil().getBits();
    }

    Value validated_super_val = Value::fromBits(validated_super_bits);
    superclass = getClass(validated_super_val);
  }

  ObjClass *klass = new ObjClass();
  klass->obj.type = ObjType::CLASS;
  klass->name = name_str;
  klass->superclass = superclass;
  klass->methods.clear();
  klass->rootShape = createRootShape();
  klass->defaultShape = klass->rootShape;

  trackObject(klass);
  return Value::object(klass).getBits();
}

void elx_class_add_method(uint64_t class_bits, uint64_t name_bits,
                          uint64_t method_bits) {
  Value class_val = Value::fromBits(class_bits);
  ObjClass *klass = getClass(class_val);
  if (!klass)
    return;

  ObjString *method_name = extractStringKey(name_bits, nullptr);
  if (!method_name)
    return;

  klass->methods[method_name] = method_bits;
}

void elx_class_prepare_field_shape(uint64_t class_bits, uint64_t name_bits) {
  Value class_val = Value::fromBits(class_bits);
  ObjClass *klass = getClass(class_val);
  if (!klass)
    return;

  ObjString *field_name = extractStringKey(name_bits, nullptr);
  if (!field_name)
    return;

  ObjShape *shape = klass->defaultShape ? klass->defaultShape : klass->rootShape;
  klass->defaultShape = shapeEnsureTransition(shape, field_name);
}

uint64_t elx_class_find_method(uint64_t class_bits, uint64_t name_bits) {
  Value class_val = Value::fromBits(class_bits);
  ObjClass *klass = getClass(class_val);
  if (!klass)
    return Value::nil().getBits();

  ObjString *method_name = extractStringKey(name_bits, nullptr);
  if (!method_name)
    return Value::nil().getBits();

  return findMethodOnClass(klass, method_name);
}

uint64_t elx_instantiate_class(uint64_t class_bits) {
  Value class_val = Value::fromBits(class_bits);
  ObjClass *klass = getClass(class_val);
  if (!klass) {
    elx_runtime_error("Can only call functions and classes.");
    return Value::nil().getBits();
  }

  ObjInstance *instance = acquireInstanceObject();
  instance->klass = klass;
  ObjShape *shape = klass ? klass->defaultShape : nullptr;
  resetInstanceFields(instance, shape);

  trackObject(instance);
  return Value::object(instance).getBits();
}

uint64_t elx_instantiate_known_class(uint64_t class_bits) {
  Value class_val = Value::fromBits(class_bits);
  if (!class_val.isObj()) {
    return Value::nil().getBits();
  }

  auto *klass = static_cast<ObjClass *>(class_val.asObj());
  ObjInstance *instance = acquireInstanceObject();
  instance->klass = klass;
  resetInstanceFields(instance, klass ? klass->defaultShape : nullptr);

  trackObject(instance);
  return Value::object(instance).getBits();
}

uint64_t elx_get_instance_class(uint64_t instance_bits) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  if (!instance) {
    elx_runtime_error("Only instances have classes.");
    return Value::nil().getBits();
  }

  ObjClass *klass = instance->klass;
  if (!klass) {
    return Value::nil().getBits();
  }

  return Value::object(klass).getBits();
}

static bool tryReadInstanceField(ObjInstance *instance, ObjShape *shape,
                                 ObjString *field_key,
                                 uint64_t *cached_shape_bits,
                                 uint64_t *cached_slot, uint64_t *out_value,
                                 size_t *out_slot = nullptr) {
  if (!instance || !field_key)
    return false;

  ObjShape *cachedShape = loadCachedShape(cached_shape_bits);
  if (shape && cachedShape == shape && instance->fieldValues &&
      instance->fieldInitialized) {
    size_t cachedSlot = loadCachedSlot(cached_slot);
    if (cachedSlot < instance->fieldCapacity &&
        instance->fieldInitialized[cachedSlot]) {
      if (out_slot)
        *out_slot = cachedSlot;
      if (out_value)
        *out_value = instance->fieldValues[cachedSlot];
      return true;
    }
  }

  size_t slot = 0;
  if (!shape || !shapeTryGetSlot(shape, field_key, &slot)) {
    return false;
  }

  if (!instance->fieldValues || !instance->fieldInitialized ||
      slot >= instance->fieldCapacity || !instance->fieldInitialized[slot]) {
    return false;
  }

  storeCachedShape(cached_shape_bits, shape);
  storeCachedSlot(cached_slot, slot);
  if (out_slot)
    *out_slot = slot;
  if (out_value)
    *out_value = instance->fieldValues[slot];
  return true;
}

int elx_prepare_property_call(uint64_t instance_bits, uint64_t name_bits,
                              uint64_t *out_target) {
  return elx_prepare_property_call_cached(instance_bits, name_bits, nullptr,
                                          out_target);
}

int elx_prepare_property_call_cached(uint64_t instance_bits, uint64_t name_bits,
                                     CallInlineCache *cache,
                                     uint64_t *out_target) {
  if (out_target) {
    *out_target = Value::nil().getBits();
  }

  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  if (!instance) {
    elx_runtime_error("Only instances have properties.");
    return -1;
  }

  ObjShape *shape = ensureInstanceShape(instance);
  if (cache && instance->klass &&
      cache->kind == static_cast<int32_t>(CallInlineCacheKind::BOUND_METHOD) &&
      cache->guard1_bits == reinterpret_cast<uint64_t>(instance->klass) &&
      cache->callee_bits == reinterpret_cast<uint64_t>(shape) &&
      cache->guard0_bits != 0) {
    if (out_target) {
      *out_target = cache->guard0_bits;
    }
    return PROPERTY_CALL_METHOD;
  }

  ObjString *field_key = extractStringKey(name_bits, nullptr);
  if (!field_key) {
    elx_runtime_error("Property name must be a string.");
    return -1;
  }

  bool hasFieldSlot = shapeTryGetSlot(shape, field_key, nullptr);
  if (hasFieldSlot) {
    uint64_t field_bits = Value::nil().getBits();
    if (tryReadInstanceField(instance, shape, field_key, nullptr, nullptr,
                             &field_bits)) {
      if (out_target) {
        *out_target = field_bits;
      }
      return PROPERTY_CALL_FIELD;
    }
  }

  uint64_t method_bits = instance->klass
                             ? findMethodOnClass(instance->klass, field_key)
                             : Value::nil().getBits();
  if (method_bits != Value::nil().getBits()) {
    if (!hasFieldSlot) {
      configureMethodCallCache(cache, method_bits, instance->klass,
                               reinterpret_cast<uint64_t>(shape));
    }
    if (out_target) {
      *out_target = method_bits;
    }
    return PROPERTY_CALL_METHOD;
  }

  std::string message = "Undefined property '" +
                        std::string(field_key->chars, field_key->length) + "'.";
  elx_runtime_error_silent(message.c_str());
  elx_emit_runtime_error();
  return -1;
}

uint64_t elx_call_prepared_property(int target_kind, uint64_t receiver_bits,
                                    uint64_t target_bits, uint64_t *args,
                                    int arg_count) {
  if (target_kind == PROPERTY_CALL_FIELD) {
    return elx_call_value(target_bits, args, arg_count);
  }

  if (target_kind == PROPERTY_CALL_METHOD) {
    return callMethodWithoutBinding(receiver_bits, target_bits, args,
                                    arg_count);
  }

  elx_runtime_error("Can only call functions and classes.");
  return Value::nil().getBits();
}

uint64_t elx_call_property(uint64_t instance_bits, uint64_t name_bits,
                           uint64_t *args, int arg_count) {
  elx_clear_runtime_error();

  uint64_t target_bits = Value::nil().getBits();
  int target_kind =
      elx_prepare_property_call(instance_bits, name_bits, &target_bits);
  if (elx_has_runtime_error()) {
    return Value::nil().getBits();
  }

  return elx_call_prepared_property(target_kind, instance_bits, target_bits,
                                    args, arg_count);
}

uint64_t elx_get_instance_field(uint64_t instance_bits, uint64_t name_bits) {
  uint64_t result = Value::nil().getBits();
  int status = elx_try_get_instance_field_cached(instance_bits, name_bits,
                                                 nullptr, nullptr, &result);
  if (status == 1) {
    return result;
  }

  std::string field_name;
  extractStringKey(name_bits, &field_name);
  std::string error_msg = "Undefined property '" + field_name + "'.";
  elx_runtime_error_silent(error_msg.c_str());
  return Value::nil().getBits();
}

static bool ensureSlotForWrite(ObjInstance *instance, ObjString *field_key,
                               uint64_t *cached_shape_bits,
                               uint64_t *cached_slot, size_t *out_slot);

uint64_t elx_set_instance_field(uint64_t instance_bits, uint64_t name_bits,
                                uint64_t value_bits) {
  return elx_set_instance_field_cached(instance_bits, name_bits, value_bits,
                                       nullptr, nullptr);
}

uint64_t elx_set_instance_field_slot(uint64_t instance_bits,
                                     uint64_t name_bits, uint32_t slot,
                                     uint64_t value_bits) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  if (!instance) {
    elx_runtime_error("Only instances have fields.");
    return Value::nil().getBits();
  }

  ObjShape *shape = ensureInstanceShape(instance);
  if (!shape || slot >= shape->slotCount) {
    ObjString *field_key = extractStringKey(name_bits, nullptr);
    if (!field_key) {
      elx_runtime_error("Property name must be a string.");
      return Value::nil().getBits();
    }

    size_t dynamicSlot = 0;
    if (!ensureSlotForWrite(instance, field_key, nullptr, nullptr,
                            &dynamicSlot)) {
      return Value::nil().getBits();
    }
    slot = static_cast<uint32_t>(dynamicSlot);
    shape = instance->shape;
  }

  size_t required = shape->slotCount;
  if (slot >= instance->fieldCapacity) {
    ensureInstanceCapacity(instance, required, true);
  }

  if (!instance->fieldValues || !instance->fieldInitialized ||
      slot >= instance->fieldCapacity) {
    return Value::nil().getBits();
  }

  instance->fieldValues[slot] = value_bits;
  instance->fieldInitialized[slot] = 1;
  return value_bits;
}

int elx_try_get_instance_field(uint64_t instance_bits, uint64_t name_bits,
                               uint64_t *out_value) {
  return elx_try_get_instance_field_cached(instance_bits, name_bits, nullptr,
                                           nullptr, out_value);
}

uint64_t elx_get_property_slow(uint64_t instance_bits, uint64_t name_bits,
                               PropertyCache *cache, uint32_t capacity) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  if (!instance) {
    elx_runtime_error("Only instances have properties.");
    return Value::nil().getBits();
  }

  ObjShape *shape = ensureInstanceShape(instance);
  size_t cachedSlot = 0;
  if (propertyCacheLookup(cache, shape, capacity, &cachedSlot) &&
      instance->fieldValues && instance->fieldInitialized &&
      cachedSlot < instance->fieldCapacity &&
      instance->fieldInitialized[cachedSlot]) {
#if defined(ELOXIR_ENABLE_CACHE_STATS)
    elx_cache_stats_record_property_hit(0);
#endif
    return instance->fieldValues[cachedSlot];
  }

#if defined(ELOXIR_ENABLE_CACHE_STATS)
  elx_cache_stats_record_property_miss(0);
#endif
  ObjString *field_key = extractStringKey(name_bits, nullptr);
  if (!field_key) {
    elx_runtime_error("Property name must be a string.");
    return Value::nil().getBits();
  }

  uint64_t value_bits = Value::nil().getBits();
  size_t slot = 0;
  if (tryReadInstanceField(instance, shape, field_key, nullptr, nullptr,
                           &value_bits, &slot)) {
    propertyCacheUpdate(cache, shape, slot, capacity, false);
    return value_bits;
  }

  uint64_t method_bits = Value::nil().getBits();
  if (instance->klass) {
    method_bits = findMethodOnClass(instance->klass, field_key);
  }

  if (method_bits != Value::nil().getBits()) {
    return elx_bind_method(instance_bits, method_bits);
  }

  std::string message = "Undefined property '" +
                        std::string(field_key->chars, field_key->length) + "'.";
  elx_runtime_error_silent(message.c_str());
  elx_emit_runtime_error();
  return Value::nil().getBits();
}

static bool ensureSlotForWrite(ObjInstance *instance, ObjString *field_key,
                               uint64_t *cached_shape_bits,
                               uint64_t *cached_slot, size_t *out_slot) {
  if (!instance || !field_key)
    return false;

  ObjShape *shape = ensureInstanceShape(instance);
  ObjShape *cachedShape = loadCachedShape(cached_shape_bits);
  size_t slot = 0;

  if (shape && cachedShape == shape) {
    slot = loadCachedSlot(cached_slot);
  } else if (shapeTryGetSlot(shape, field_key, &slot)) {
  } else {
    ObjShape *next = shapeEnsureTransition(shape, field_key);
    if (instance->klass && instance->klass->defaultShape == shape) {
      instance->klass->defaultShape = next;
    }
    shape = next;
    instance->shape = shape;
    slot = shape ? (shape->slotCount - 1) : 0;
  }

  if (shape) {
    storeCachedShape(cached_shape_bits, shape);
  }
  storeCachedSlot(cached_slot, slot);

  size_t required = shape ? shape->slotCount : (slot + 1);
  if (slot >= instance->fieldCapacity) {
    ensureInstanceCapacity(instance, required, true);
  }

  if (!instance->fieldValues || !instance->fieldInitialized ||
      slot >= instance->fieldCapacity) {
    return false;
  }

  if (out_slot)
    *out_slot = slot;
  return true;
}

int elx_try_get_instance_field_cached(uint64_t instance_bits,
                                      uint64_t name_bits,
                                      uint64_t *cached_shape_bits,
                                      uint64_t *cached_slot,
                                      uint64_t *out_value) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  if (!instance) {
    elx_runtime_error("Only instances have properties.");
    return -1;
  }

  ObjString *field_key = extractStringKey(name_bits, nullptr);
  if (!field_key) {
    elx_runtime_error("Property name must be a string.");
    return -1;
  }

  ObjShape *shape = ensureInstanceShape(instance);
  if (tryReadInstanceField(instance, shape, field_key, cached_shape_bits,
                           cached_slot, out_value)) {
    return 1;
  }

  std::string message = "Undefined property '" +
                        std::string(field_key->chars, field_key->length) + "'.";
  elx_runtime_error_silent(message.c_str());
  elx_emit_runtime_error();
  return Value::nil().getBits();
}

uint64_t elx_set_property_slow(uint64_t instance_bits, uint64_t name_bits,
                               uint64_t value_bits, PropertyCache *cache,
                               uint32_t capacity) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  if (!instance) {
    elx_runtime_error("Only instances have fields.");
    return Value::nil().getBits();
  }

  ObjShape *shape = ensureInstanceShape(instance);
  size_t cachedSlot = 0;
  if (propertyCacheLookup(cache, shape, capacity, &cachedSlot) &&
      instance->fieldValues && instance->fieldInitialized &&
      cachedSlot < instance->fieldCapacity) {
    instance->fieldValues[cachedSlot] = value_bits;
    instance->fieldInitialized[cachedSlot] = 1;
#if defined(ELOXIR_ENABLE_CACHE_STATS)
    elx_cache_stats_record_property_hit(1);
#endif
    return value_bits;
  }

#if defined(ELOXIR_ENABLE_CACHE_STATS)
  elx_cache_stats_record_property_miss(1);
#endif
  ObjString *field_key = extractStringKey(name_bits, nullptr);
  if (!field_key) {
    elx_runtime_error("Property name must be a string.");
    return Value::nil().getBits();
  }

  size_t slot = 0;
  if (!ensureSlotForWrite(instance, field_key, nullptr, nullptr, &slot)) {
    return Value::nil().getBits();
  }

  if (!instance->fieldValues || !instance->fieldInitialized ||
      slot >= instance->fieldCapacity) {
    return Value::nil().getBits();
  }

  instance->fieldValues[slot] = value_bits;
  instance->fieldInitialized[slot] = 1;
  propertyCacheUpdate(cache, instance->shape, slot, capacity, true);
  return value_bits;
}

uint64_t elx_set_instance_field_cached(uint64_t instance_bits,
                                       uint64_t name_bits, uint64_t value_bits,
                                       uint64_t *cached_shape_bits,
                                       uint64_t *cached_slot) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  if (!instance) {
    elx_runtime_error("Only instances have fields.");
    return Value::nil().getBits();
  }

  ObjString *field_key = extractStringKey(name_bits, nullptr);
  if (!field_key) {
    elx_runtime_error("Property name must be a string.");
    return Value::nil().getBits();
  }

  size_t slot = 0;
  if (!ensureSlotForWrite(instance, field_key, cached_shape_bits, cached_slot,
                          &slot)) {
    return Value::nil().getBits();
  }

  instance->fieldValues[slot] = value_bits;
  instance->fieldInitialized[slot] = 1;
  return value_bits;
}

eloxir::ObjShape *elx_instance_shape_ptr(uint64_t instance_bits) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  return instance ? instance->shape : nullptr;
}

uint64_t *elx_instance_field_values_ptr(uint64_t instance_bits) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  return instance ? instance->fieldValues : nullptr;
}

uint8_t *elx_instance_field_presence_ptr(uint64_t instance_bits) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  return instance ? instance->fieldInitialized : nullptr;
}

uint64_t elx_bind_method(uint64_t instance_bits, uint64_t method_bits) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  if (!instance)
    return Value::nil().getBits();

  Value method_val = Value::fromBits(method_bits);
  ObjClosure *closure = getClosure(method_val);
  if (!closure)
    return method_bits;

  ObjBoundMethod *bound = new ObjBoundMethod();
  bound->obj.type = ObjType::BOUND_METHOD;
  bound->receiver = instance_bits;
  bound->method = method_bits;

  trackObject(bound);
  return Value::object(bound).getBits();
}

void elx_set_object_tracking_enabled(int enabled) {
  object_tracking_enabled = enabled != 0;
}

void elx_cleanup_all_objects() {
  if (!object_tracking_enabled)
    return;

  // Free all tracked objects except global built-ins and interned strings
  std::unordered_set<void *> persistent_objects;

  // Collect built-in objects that should not be freed
  for (const auto &pair : global_builtins) {
    Value v = Value::fromBits(pair.second);
    if (v.isObj()) {
      persistent_objects.insert(v.asObj());
    }
  }

  // Collect interned strings that should not be freed
  for (const auto &pair : global_interned_strings) {
    Value v = Value::fromBits(pair.second);
    if (v.isObj()) {
      persistent_objects.insert(v.asObj());
    }
  }

  std::vector<void *> remaining_objects;
  remaining_objects.reserve(persistent_objects.size());

  // Free non-persistent objects
  for (void *obj : allocated_objects) {
    if (persistent_objects.find(obj) == persistent_objects.end()) {
      destroyObject(static_cast<Obj *>(obj));
    } else {
      remaining_objects.push_back(obj);
    }
  }

  // Clear the registry but keep persistent objects alive
  allocated_objects = std::move(remaining_objects);
}

void elx_initialize_global_builtins() {
  if (global_builtins_initialized) {
    return; // Already initialized
  }

  // Initialize clock function - create the actual function object
  auto clock_obj = elx_allocate_native(
      "clock", 0, [](uint64_t *, int) -> uint64_t { return elx_clock(); });
  global_builtins["clock"] = clock_obj;

  auto readLine_obj =
      elx_allocate_native("readLine", 0, [](uint64_t *, int) -> uint64_t {
        return elx_readLine();
      });
  global_builtins["readLine"] = readLine_obj;

  global_builtins_initialized = true;
}

uint64_t elx_get_global_builtin(const char *name) {
  elx_initialize_global_builtins(); // Ensure builtins are initialized

  auto it = global_builtins.find(std::string(name));
  if (it != global_builtins.end()) {
    return it->second;
  }

  return Value::nil().getBits(); // Return nil if not found
}

// Global environment functions for cross-line persistence
void elx_set_global_variable(const char *name, uint64_t value) {
  global_variables[std::string(name)] = value;
}

uint64_t elx_get_global_variable(const char *name) {
  auto it = global_variables.find(std::string(name));
  if (it != global_variables.end()) {
    return it->second;
  }
  return Value::nil().getBits(); // Return nil if not found
}

int elx_has_global_variable(const char *name) {
  return global_variables.find(std::string(name)) != global_variables.end() ? 1
                                                                            : 0;
}

void elx_set_global_function(const char *name, uint64_t func_obj) {
  if (!name)
    return;

  global_functions[std::string(name)] = func_obj;
}

uint64_t elx_get_global_function(const char *name) {
  if (!name)
    return Value::nil().getBits();

  auto it = global_functions.find(std::string(name));
  if (it != global_functions.end()) {
    return it->second;
  }
  return Value::nil().getBits(); // Return nil if not found
}

int elx_has_global_function(const char *name) {
  return global_functions.find(std::string(name)) != global_functions.end() ? 1
                                                                            : 0;
}

// Error handling functions
static void set_runtime_error(const char *message, bool print_immediately) {
  elx_runtime_error_flag = 1;
  runtime_error_message = std::string(message);
  if (print_immediately) {
    std::cerr << "Runtime error: " << message << std::endl;
  }
}

void elx_runtime_error(const char *message) {
  set_runtime_error(message, true);
}

void elx_runtime_error_silent(const char *message) {
  set_runtime_error(message, false);
}

void elx_emit_runtime_error() {
  if (!elx_runtime_error_flag)
    return;

  const char *message = runtime_error_message.c_str();
  std::cerr << "Runtime error: " << message << std::endl;
}

int elx_has_runtime_error() { return elx_runtime_error_flag ? 1 : 0; }

void elx_clear_runtime_error() {
  elx_runtime_error_flag = 0;
  runtime_error_message.clear();
}

int elx_enter_call_frame() {
  if (elx_current_call_depth >= MAX_CALL_DEPTH) {
    elx_runtime_error("Stack overflow.");
    return 0;
  }
  ++elx_current_call_depth;
  return 1;
}

void elx_leave_call_frame() {
  if (elx_current_call_depth > 0) {
    --elx_current_call_depth;
  }
}

// Safe arithmetic operations (IEEE 754 compliant)
uint64_t elx_safe_divide(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);

  if (!a.isNum() || !b.isNum()) {
    elx_runtime_error("Operands must be numbers.");
    return Value::nil().getBits();
  }

  // Perform IEEE 754 compliant division
  // This will produce NaN for 0/0, +inf for positive/0, -inf for negative/0
  double dividend = a.asNum();
  double divisor = b.asNum();
  double result = dividend / divisor;

  return Value::number(result).getBits();
}

uint64_t elx_add_values(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);
  if (a.isNum() && b.isNum()) {
    return Value::number(a.asNum() + b.asNum()).getBits();
  }
  if (getStringObject(a) && getStringObject(b)) {
    return elx_concatenate_strings(a_bits, b_bits);
  }
  elx_runtime_error("Operands must be numbers or strings for +.");
  return Value::nil().getBits();
}

uint64_t elx_subtract_values(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);
  if (!a.isNum() || !b.isNum()) {
    elx_runtime_error("Operands must be numbers.");
    return Value::nil().getBits();
  }
  return Value::number(a.asNum() - b.asNum()).getBits();
}

uint64_t elx_multiply_values(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);
  if (!a.isNum() || !b.isNum()) {
    elx_runtime_error("Operands must be numbers.");
    return Value::nil().getBits();
  }
  return Value::number(a.asNum() * b.asNum()).getBits();
}

uint64_t elx_divide_values(uint64_t a_bits, uint64_t b_bits) {
  return elx_safe_divide(a_bits, b_bits);
}

uint64_t elx_greater_values(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);
  if (!a.isNum() || !b.isNum()) {
    elx_runtime_error("Operands must be numbers.");
    return Value::nil().getBits();
  }
  return Value::boolean(a.asNum() > b.asNum()).getBits();
}

uint64_t elx_greater_equal_values(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);
  if (!a.isNum() || !b.isNum()) {
    elx_runtime_error("Operands must be numbers.");
    return Value::nil().getBits();
  }
  return Value::boolean(a.asNum() >= b.asNum()).getBits();
}

uint64_t elx_less_values(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);
  if (!a.isNum() || !b.isNum()) {
    elx_runtime_error("Operands must be numbers.");
    return Value::nil().getBits();
  }
  return Value::boolean(a.asNum() < b.asNum()).getBits();
}

uint64_t elx_less_equal_values(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);
  if (!a.isNum() || !b.isNum()) {
    elx_runtime_error("Operands must be numbers.");
    return Value::nil().getBits();
  }
  return Value::boolean(a.asNum() <= b.asNum()).getBits();
}

uint64_t elx_equal_values(uint64_t a_bits, uint64_t b_bits) {
  Value a = Value::fromBits(a_bits);
  Value b = Value::fromBits(b_bits);
  if (a.tag() != b.tag()) {
    return Value::boolean(false).getBits();
  }
  if (a.isNum()) {
    return Value::boolean(a.asNum() == b.asNum()).getBits();
  }
  if (getStringObject(a) && getStringObject(b)) {
    return Value::boolean(elx_strings_equal_interned(a_bits, b_bits) != 0)
        .getBits();
  }
  return Value::boolean(a_bits == b_bits).getBits();
}

uint64_t elx_not_equal_values(uint64_t a_bits, uint64_t b_bits) {
  Value equal = Value::fromBits(elx_equal_values(a_bits, b_bits));
  return Value::boolean(!equal.asBool()).getBits();
}

uint64_t elx_negate_value(uint64_t value_bits) {
  Value value = Value::fromBits(value_bits);
  if (!value.isNum()) {
    elx_runtime_error("Operand must be a number for negation.");
    return Value::nil().getBits();
  }
  return Value::number(-value.asNum()).getBits();
}

uint64_t *elx_allocate_value_slot(uint64_t initial_value) {
  auto *slot = static_cast<uint64_t *>(malloc(sizeof(uint64_t)));
  if (!slot) {
    elx_runtime_error("Out of memory.");
    return nullptr;
  }
  *slot = initial_value;
  return slot;
}
