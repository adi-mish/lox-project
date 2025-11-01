#include "RuntimeAPI.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace eloxir;

// Simple object registry for tracking allocations
static std::unordered_set<void *> allocated_objects;

// Global built-ins registry
static std::unordered_map<std::string, uint64_t> global_builtins;
static bool global_builtins_initialized = false;

// Global string interning table
static std::unordered_map<std::string, uint64_t> global_interned_strings;

// Global environment for cross-line persistence
static std::unordered_map<std::string, uint64_t> global_variables;
static std::unordered_map<std::string, uint64_t> global_functions;

// Runtime error state
static bool runtime_error_flag = false;
static std::string runtime_error_message;

namespace {
constexpr int MAX_CALL_DEPTH = 256;
thread_local int current_call_depth = 0;

struct CallDepthGuard {
  CallDepthGuard() {
    if (current_call_depth < MAX_CALL_DEPTH) {
      ++current_call_depth;
      active = true;
    }
  }

  ~CallDepthGuard() {
    if (active) {
      --current_call_depth;
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
} // namespace

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

static uint64_t findMethodOnClass(ObjClass *klass, ObjString *name);

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
    delete reinterpret_cast<ObjClass *>(obj);
    break;
  case ObjType::INSTANCE:
    delete reinterpret_cast<ObjInstance *>(obj);
    break;
  case ObjType::BOUND_METHOD:
    delete reinterpret_cast<ObjBoundMethod *>(obj);
    break;
  case ObjType::NATIVE:
    delete reinterpret_cast<ObjNative *>(obj);
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
    case ObjType::NATIVE: {
      std::cout << "<native fn>";
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
  using namespace std::chrono;
  auto secs = duration<double>(system_clock::now().time_since_epoch()).count();
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
  // Create a std::string for lookup in the intern table
  std::string str(chars, length);

  // Check if this string is already interned globally
  auto intern_it = global_interned_strings.find(str);
  if (intern_it != global_interned_strings.end()) {
    return intern_it->second; // Return the existing interned string
  }

  // If not found, create a new string object
  uint64_t new_string = elx_allocate_string(chars, length);

  // Add it to the global intern table
  global_interned_strings[str] = new_string;

  return new_string;
}

uint64_t elx_allocate_string(const char *chars, int length) {
  // Allocate memory for the string object
  size_t size = sizeof(ObjString) + length + 1; // +1 for null terminator
  ObjString *str = static_cast<ObjString *>(malloc(size));
  if (!str) {
    return Value::nil().getBits();
  }

  str->obj.type = ObjType::STRING;
  str->length = length;
  std::memcpy(str->chars, chars, length);
  str->chars[length] = '\0'; // null terminate

  // Track the allocation
  allocated_objects.insert(str);

  return Value::object(str).getBits();
}

void elx_free_object(uint64_t obj_bits) {
  Value v = Value::fromBits(obj_bits);
  if (!v.isObj())
    return;

  Obj *obj = static_cast<Obj *>(v.asObj());

  // Remove from tracking registry
  allocated_objects.erase(obj);

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
  size_t size = sizeof(ObjString) + new_length + 1;
  ObjString *result = static_cast<ObjString *>(malloc(size));

  result->obj.type = ObjType::STRING;
  result->length = new_length;

  std::memcpy(result->chars, str_a->chars, str_a->length);
  std::memcpy(result->chars + str_a->length, str_b->chars, str_b->length);
  result->chars[new_length] = '\0';

  // Track the allocation
  allocated_objects.insert(result);

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

int elx_value_is_string(uint64_t value_bits) {
  Value value = Value::fromBits(value_bits);
  ObjString *string_obj = getStringObject(value);
  return string_obj != nullptr ? 1 : 0;
}

uint64_t elx_allocate_function(const char *name, int arity,
                               void *llvm_function) {
  // Allocate memory for the function object
  size_t name_len = 0;
  if (name) {
    // Calculate string length manually
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
  func->llvm_function = llvm_function;

  // Copy the name after the function struct
  char *name_storage = reinterpret_cast<char *>(func + 1);
  if (name) {
    std::memcpy(name_storage, name, name_len);
  }
  name_storage[name_len] = '\0';
  func->name = name_storage;

  // Track the allocation
  allocated_objects.insert(func);

  return Value::object(func).getBits();
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

  // Use a more flexible calling mechanism that supports up to 16 arguments
  // For functions with more arguments, we'd need libffi or similar
  try {
    switch (arg_count) {
    case 0: {
      typedef uint64_t (*FunctionPtr0)();
      FunctionPtr0 fn = reinterpret_cast<FunctionPtr0>(func->llvm_function);
      return fn();
    }
    case 1: {
      typedef uint64_t (*FunctionPtr1)(uint64_t);
      FunctionPtr1 fn = reinterpret_cast<FunctionPtr1>(func->llvm_function);
      return fn(args[0]);
    }
    case 2: {
      typedef uint64_t (*FunctionPtr2)(uint64_t, uint64_t);
      FunctionPtr2 fn = reinterpret_cast<FunctionPtr2>(func->llvm_function);
      return fn(args[0], args[1]);
    }
    case 3: {
      typedef uint64_t (*FunctionPtr3)(uint64_t, uint64_t, uint64_t);
      FunctionPtr3 fn = reinterpret_cast<FunctionPtr3>(func->llvm_function);
      return fn(args[0], args[1], args[2]);
    }
    case 4: {
      typedef uint64_t (*FunctionPtr4)(uint64_t, uint64_t, uint64_t, uint64_t);
      FunctionPtr4 fn = reinterpret_cast<FunctionPtr4>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3]);
    }
    case 5: {
      typedef uint64_t (*FunctionPtr5)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t);
      FunctionPtr5 fn = reinterpret_cast<FunctionPtr5>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4]);
    }
    case 6: {
      typedef uint64_t (*FunctionPtr6)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t);
      FunctionPtr6 fn = reinterpret_cast<FunctionPtr6>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5]);
    }
    case 7: {
      typedef uint64_t (*FunctionPtr7)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t);
      FunctionPtr7 fn = reinterpret_cast<FunctionPtr7>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
    }
    case 8: {
      typedef uint64_t (*FunctionPtr8)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t);
      FunctionPtr8 fn = reinterpret_cast<FunctionPtr8>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7]);
    }
    case 9: {
      typedef uint64_t (*FunctionPtr9)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t);
      FunctionPtr9 fn = reinterpret_cast<FunctionPtr9>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8]);
    }
    case 10: {
      typedef uint64_t (*FunctionPtr10)(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t);
      FunctionPtr10 fn = reinterpret_cast<FunctionPtr10>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9]);
    }
    case 11: {
      typedef uint64_t (*FunctionPtr11)(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t);
      FunctionPtr11 fn = reinterpret_cast<FunctionPtr11>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10]);
    }
    case 12: {
      typedef uint64_t (*FunctionPtr12)(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t);
      FunctionPtr12 fn = reinterpret_cast<FunctionPtr12>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10], args[11]);
    }
    case 13: {
      typedef uint64_t (*FunctionPtr13)(
          uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
          uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
      FunctionPtr13 fn = reinterpret_cast<FunctionPtr13>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10], args[11], args[12]);
    }
    case 14: {
      typedef uint64_t (*FunctionPtr14)(
          uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
          uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
      FunctionPtr14 fn = reinterpret_cast<FunctionPtr14>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10], args[11], args[12],
                args[13]);
    }
    case 15: {
      typedef uint64_t (*FunctionPtr15)(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t);
      FunctionPtr15 fn = reinterpret_cast<FunctionPtr15>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10], args[11], args[12],
                args[13], args[14]);
    }
    case 16: {
      typedef uint64_t (*FunctionPtr16)(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t, uint64_t);
      FunctionPtr16 fn = reinterpret_cast<FunctionPtr16>(func->llvm_function);
      return fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                args[7], args[8], args[9], args[10], args[11], args[12],
                args[13], args[14], args[15]);
    }
    default: {
      // For functions with more than 16 arguments, we need libffi or similar
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

  ObjNative *native = new ObjNative();
  native->obj.type = ObjType::NATIVE;
  native->function = function;
  native->name = name;
  native->arity = arity;

  allocated_objects.insert(native);
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
  case ObjType::CLOSURE:
    return elx_call_closure(callee_bits, args, arg_count);
  case ObjType::NATIVE:
    return elx_call_native(callee_bits, args, arg_count);
  case ObjType::CLASS: {
    ObjClass *klass = static_cast<ObjClass *>(obj_ptr);

    uint64_t instance_bits = elx_instantiate_class(callee_bits);
    if (elx_has_runtime_error())
      return Value::nil().getBits();

    uint64_t init_bits = elx_intern_string("init", 4);
    ObjString *init_name = getStringObject(Value::fromBits(init_bits));
    uint64_t initializer_bits =
        init_name ? findMethodOnClass(klass, init_name) : Value::nil().getBits();
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

      uint64_t bound_bits = elx_bind_method(instance_bits, initializer_bits);
      if (elx_has_runtime_error())
        return Value::nil().getBits();

      elx_call_value(bound_bits, args, arg_count);
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

    Value method_val = Value::fromBits(bound->method);
    ObjClosure *closure = getClosure(method_val);
    ObjFunction *func =
        closure ? closure->function : getFunction(method_val);

    const char *method_name = (func && func->name && func->name[0] != '\0')
                                  ? func->name
                                  : "<anonymous>";
    if (func) {
      int expected_total = func->arity;
      int expected_user_args = expected_total > 0 ? expected_total - 1 : 0;
      if (arg_count != expected_user_args) {
        std::string error_msg =
            formatArityError(method_name, expected_user_args, arg_count);
        elx_runtime_error(error_msg.c_str());
        return Value::nil().getBits();
      }
    }

    std::vector<uint64_t> method_args(static_cast<size_t>(arg_count) + 1);
    method_args[0] = bound->receiver;
    for (int i = 0; i < arg_count; ++i) {
      method_args[i + 1] = args ? args[i] : Value::nil().getBits();
    }

    int call_arg_count = static_cast<int>(method_args.size());
    if (closure) {
      return elx_call_closure(bound->method, method_args.data(),
                              call_arg_count);
    }

    if (func) {
      return elx_call_function(bound->method, method_args.data(),
                               call_arg_count);
    }

    return elx_call_value(bound->method, method_args.data(), call_arg_count);
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
  allocated_objects.insert(created_upvalue);

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
  allocated_objects.insert(created_upvalue);

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
  allocated_objects.insert(closure);

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

  if (!func->llvm_function) {
    elx_runtime_error("Closure function has no implementation.");
    return Value::nil().getBits();
  }

  if (closure->upvalue_count == 0) {
    return elx_call_function(Value::object(func).getBits(), args, arg_count);
  }

  CallDepthGuard depth_guard;
  if (!depth_guard.entered()) {
    elx_runtime_error("Stack overflow.");
    return Value::nil().getBits();
  }

  // Create upvalue array for function call
  uint64_t *upvalue_args = nullptr;
  if (closure->upvalue_count > 0) {
    upvalue_args = static_cast<uint64_t *>(
        malloc(sizeof(uint64_t) * closure->upvalue_count));
    if (!upvalue_args) {
      elx_runtime_error("Failed to allocate upvalue arguments.");
      return Value::nil().getBits();
    }

    // Pass upvalue objects through to the JITed function so it can fetch and
    // update them via the runtime helpers.
    for (int i = 0; i < closure->upvalue_count; i++) {
      if (closure->upvalues[i] != nullptr) {
        upvalue_args[i] = Value::object(closure->upvalues[i]).getBits();
      } else {
        upvalue_args[i] = Value::nil().getBits();
      }
    }
  }

  try {
    uint64_t result;
    // Call function with original args plus upvalues pointer as the final
    // parameter. Support up to 16 user arguments (matching elx_call_function).
    switch (arg_count) {
    case 0: {
      typedef uint64_t (*FunctionPtr)(uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(upvalue_args);
      break;
    }
    case 1: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], upvalue_args);
      break;
    }
    case 2: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], upvalue_args);
      break;
    }
    case 3: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], upvalue_args);
      break;
    }
    case 4: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], upvalue_args);
      break;
    }
    case 5: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], upvalue_args);
      break;
    }
    case 6: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], args[5],
                  upvalue_args);
      break;
    }
    case 7: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t,
                                      uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                  upvalue_args);
      break;
    }
    case 8: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                  args[7], upvalue_args);
      break;
    }
    case 9: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                  args[7], args[8], upvalue_args);
      break;
    }
    case 10: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                  args[7], args[8], args[9], upvalue_args);
      break;
    }
    case 11: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                  args[7], args[8], args[9], args[10], upvalue_args);
      break;
    }
    case 12: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                  args[7], args[8], args[9], args[10], args[11], upvalue_args);
      break;
    }
    case 13: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                  args[7], args[8], args[9], args[10], args[11], args[12],
                  upvalue_args);
      break;
    }
    case 14: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                  args[7], args[8], args[9], args[10], args[11], args[12],
                  args[13], upvalue_args);
      break;
    }
    case 15: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                  args[7], args[8], args[9], args[10], args[11], args[12],
                  args[13], args[14], upvalue_args);
      break;
    }
    case 16: {
      typedef uint64_t (*FunctionPtr)(uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t, uint64_t, uint64_t, uint64_t,
                                      uint64_t *);
      FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
      result = fn(args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                  args[7], args[8], args[9], args[10], args[11], args[12],
                  args[13], args[14], args[15], upvalue_args);
      break;
    }
    default: {
      std::string error_msg = "Closures with " + std::to_string(arg_count) +
                              " arguments are not yet fully supported.";
      elx_runtime_error(error_msg.c_str());
      result = Value::nil().getBits();
    }
    }

    if (upvalue_args) {
      free(upvalue_args);
    }
    return result;

  } catch (const std::exception &e) {
    if (upvalue_args) {
      free(upvalue_args);
    }
    std::string error_msg =
        "Exception during closure call: " + std::string(e.what());
    elx_runtime_error(error_msg.c_str());
    return Value::nil().getBits();
  } catch (...) {
    if (upvalue_args) {
      free(upvalue_args);
    }
    elx_runtime_error("Unknown exception during closure call.");
    return Value::nil().getBits();
  }
}

int elx_is_function(uint64_t value_bits) {
  Value v = Value::fromBits(value_bits);
  ObjFunction *func = getFunction(v);
  if (func)
    return 1;

  ObjNative *native = getNative(v);
  bool result = native != nullptr;
  return result ? 1 : 0;
}

int elx_is_closure(uint64_t value_bits) {
  Value v = Value::fromBits(value_bits);
  ObjClosure *closure = getClosure(v);
  bool result = closure != nullptr;
  return result ? 1 : 0;
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
    return Value::nil().getBits();
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
  if (!superclass_val.isNil()) {
    superclass = getClass(superclass_val);
    if (!superclass) {
      elx_runtime_error("Superclass must be a class.");
      return Value::nil().getBits();
    }
  }

  ObjClass *klass = new ObjClass();
  klass->obj.type = ObjType::CLASS;
  klass->name = name_str;
  klass->superclass = superclass;
  klass->methods.clear();
  klass->fieldSlots.clear();

  allocated_objects.insert(klass);
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

  ObjInstance *instance = new ObjInstance();
  instance->obj.type = ObjType::INSTANCE;
  instance->klass = klass;
  size_t slotCount = klass ? klass->fieldSlots.size() : 0;
  instance->fieldValues.assign(slotCount, Value::nil().getBits());
  instance->fieldPresence.assign(slotCount, 0);

  allocated_objects.insert(instance);
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

uint64_t elx_get_instance_field(uint64_t instance_bits, uint64_t name_bits) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  if (!instance) {
    elx_runtime_error("Only instances have properties.");
    return Value::nil().getBits();
  }

  std::string field_name;
  ObjString *field_key = extractStringKey(name_bits, &field_name);
  if (!field_key) {
    elx_runtime_error("Property name must be a string.");
    return Value::nil().getBits();
  }

  ObjClass *klass = instance->klass;
  if (klass) {
    auto slotIt = klass->fieldSlots.find(field_key);
    if (slotIt != klass->fieldSlots.end()) {
      size_t slot = slotIt->second;
      if (slot < instance->fieldValues.size() &&
          slot < instance->fieldPresence.size() &&
          instance->fieldPresence[slot]) {
        return instance->fieldValues[slot];
      }
    }
  }

  std::string error_msg = "Undefined property '" + field_name + "'.";
  elx_runtime_error_silent(error_msg.c_str());
  return Value::nil().getBits();
}

uint64_t elx_set_instance_field(uint64_t instance_bits, uint64_t name_bits,
                                uint64_t value_bits) {
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

  ObjClass *klass = instance->klass;
  size_t slot = 0;
  if (klass) {
    auto slotIt = klass->fieldSlots.find(field_key);
    if (slotIt == klass->fieldSlots.end()) {
      slot = klass->fieldSlots.size();
      klass->fieldSlots[field_key] = slot;
    } else {
      slot = slotIt->second;
    }
  }

  if (instance->fieldValues.size() <= slot) {
    instance->fieldValues.resize(slot + 1, Value::nil().getBits());
    instance->fieldPresence.resize(slot + 1, 0);
  }

  instance->fieldValues[slot] = value_bits;
  instance->fieldPresence[slot] = 1;
  return value_bits;
}

int elx_try_get_instance_field(uint64_t instance_bits, uint64_t name_bits,
                               uint64_t *out_value) {
  Value instance_val = Value::fromBits(instance_bits);
  ObjInstance *instance = getInstance(instance_val);
  if (!instance) {
    elx_runtime_error("Only instances have properties.");
    return -1;
  }

  std::string field_name;
  ObjString *field_key = extractStringKey(name_bits, &field_name);
  if (!field_key) {
    elx_runtime_error("Property name must be a string.");
    return -1;
  }

  ObjClass *klass = instance->klass;
  if (klass) {
    auto slotIt = klass->fieldSlots.find(field_key);
    if (slotIt != klass->fieldSlots.end()) {
      size_t slot = slotIt->second;
      if (slot < instance->fieldValues.size() &&
          slot < instance->fieldPresence.size() &&
          instance->fieldPresence[slot]) {
        if (out_value)
          *out_value = instance->fieldValues[slot];
        return 1;
      }
    }
  }

  return 0;
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

  allocated_objects.insert(bound);
  return Value::object(bound).getBits();
}

void elx_cleanup_all_objects() {
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

  std::unordered_set<void *> remaining_objects;

  // Free non-persistent objects
  for (void *obj : allocated_objects) {
    if (persistent_objects.find(obj) == persistent_objects.end()) {
      destroyObject(static_cast<Obj *>(obj));
    } else {
      remaining_objects.insert(obj);
    }
  }

  // Clear the registry but keep persistent objects alive
  allocated_objects = remaining_objects;
}

void elx_initialize_global_builtins() {
  if (global_builtins_initialized) {
    return; // Already initialized
  }

  // Initialize clock function - create the actual function object
  auto clock_obj = elx_allocate_native(
      "clock", 0,
      [](uint64_t *, int) -> uint64_t { return elx_clock(); });
  global_builtins["clock"] = clock_obj;

  auto readLine_obj = elx_allocate_native(
      "readLine", 0,
      [](uint64_t *, int) -> uint64_t { return elx_readLine(); });
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
  runtime_error_flag = true;
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
  if (!runtime_error_flag)
    return;

  const char *message = runtime_error_message.c_str();
  std::cerr << "Runtime error: " << message << std::endl;
}

int elx_has_runtime_error() { return runtime_error_flag ? 1 : 0; }

void elx_clear_runtime_error() {
  runtime_error_flag = false;
  runtime_error_message.clear();
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
