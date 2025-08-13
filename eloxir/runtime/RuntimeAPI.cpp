#include "RuntimeAPI.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
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

  free(obj);
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
  Value func_val = Value::fromBits(func_bits);
  ObjFunction *func = getFunction(func_val);

  if (!func) {
    // Not a function - this should be a runtime error
    std::string error_msg =
        "Runtime error: Can only call functions and classes.";
    auto error_str = elx_allocate_string(error_msg.c_str(), error_msg.length());
    elx_print(error_str);
    return Value::nil().getBits();
  }

  if (arg_count != func->arity) {
    // Wrong number of arguments - runtime error
    std::string error_msg =
        "Runtime error: Expected " + std::to_string(func->arity) +
        " arguments but got " + std::to_string(arg_count) + ".";
    auto error_str = elx_allocate_string(error_msg.c_str(), error_msg.length());
    elx_print(error_str);
    return Value::nil().getBits();
  }

  // Call the LLVM function using a more flexible approach
  if (func->llvm_function) {
    // Use assembly or a more flexible calling convention
    // For now, let's support up to 8 arguments (doubled from 4)
    // This could be extended further or use a completely dynamic approach
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
    default: {
      // For functions with more than 8 arguments, we need a more sophisticated
      // approach We can use libffi or inline assembly to make truly dynamic
      // calls For now, let's at least provide a more informative error
      std::string error_msg;
      if (arg_count <= 255) { // Lox supports up to 255 parameters
        error_msg = "Runtime error: Functions with " +
                    std::to_string(arg_count) +
                    " arguments are not yet supported. Maximum supported: 8.";
      } else {
        error_msg = "Runtime error: Function arity (" +
                    std::to_string(arg_count) +
                    ") exceeds Lox limit of 255 parameters.";
      }
      auto error_str =
          elx_allocate_string(error_msg.c_str(), error_msg.length());
      elx_print(error_str);
      return Value::nil().getBits();
    }
    }
  }

  return Value::nil().getBits();
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

  // Free non-persistent objects
  for (void *obj : allocated_objects) {
    if (persistent_objects.find(obj) == persistent_objects.end()) {
      free(obj);
    }
  }

  // Clear the registry but keep persistent objects alive
  allocated_objects = persistent_objects;
}

void elx_initialize_global_builtins() {
  if (global_builtins_initialized) {
    return; // Already initialized
  }

  // Initialize clock function - create the actual function object
  auto clock_obj =
      elx_allocate_function("clock", 0, reinterpret_cast<void *>(&elx_clock));
  global_builtins["clock"] = clock_obj;

  // Initialize readLine function - create the actual function object
  auto readLine_obj = elx_allocate_function(
      "readLine", 0, reinterpret_cast<void *>(&elx_readLine));
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
  return global_variables.find(std::string(name)) != global_variables.end() ? 1 : 0;
}

void elx_set_global_function(const char *name, uint64_t func_obj) {
  global_functions[std::string(name)] = func_obj;
}

uint64_t elx_get_global_function(const char *name) {
  auto it = global_functions.find(std::string(name));
  if (it != global_functions.end()) {
    return it->second;
  }
  return Value::nil().getBits(); // Return nil if not found
}

int elx_has_global_function(const char *name) {
  return global_functions.find(std::string(name)) != global_functions.end() ? 1 : 0;
}
