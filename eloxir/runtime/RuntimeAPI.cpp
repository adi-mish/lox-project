#include "RuntimeAPI.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

using namespace eloxir;

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

  return Value::object(str).getBits();
}

void elx_free_object(uint64_t obj_bits) {
  Value v = Value::fromBits(obj_bits);
  if (!v.isObj())
    return;

  Obj *obj = static_cast<Obj *>(v.asObj());
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

  return Value::object(func).getBits();
}

uint64_t elx_call_function(uint64_t func_bits, uint64_t *args, int arg_count) {
  Value func_val = Value::fromBits(func_bits);
  ObjFunction *func = getFunction(func_val);

  if (!func) {
    // Not a function
    return Value::nil().getBits();
  }

  if (arg_count != func->arity) {
    // Wrong number of arguments - should be a runtime error
    // For now, return nil
    return Value::nil().getBits();
  }

  // Call the LLVM function
  if (func->llvm_function) {
    // For now, assume all functions take no arguments and return uint64_t
    typedef uint64_t (*FunctionPtr)();
    FunctionPtr fn = reinterpret_cast<FunctionPtr>(func->llvm_function);
    return fn();
  }

  return Value::nil().getBits();
}
