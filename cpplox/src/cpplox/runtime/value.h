#ifndef clox_value_h
#define clox_value_h

#include <cstring>
#include <vector>

#include "common.h"

namespace cpplox {

struct Obj;
struct ObjString;

#ifdef NAN_BOXING

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN ((uint64_t)0x7ffc000000000000)

#define TAG_NIL 1
#define TAG_FALSE 2
#define TAG_TRUE 3
#define TAG_UNINITIALIZED 4

class Value {
public:
  constexpr Value() : bits_(QNAN | TAG_NIL) {}

  static constexpr Value fromBits(uint64_t bits) { return Value(bits); }
  static constexpr Value boolean(bool value) {
    return fromBits(QNAN | (value ? TAG_TRUE : TAG_FALSE));
  }
  static constexpr Value nil() { return fromBits(QNAN | TAG_NIL); }
  static constexpr Value uninitialized() {
    return fromBits(QNAN | TAG_UNINITIALIZED);
  }
  static constexpr Value object(Obj *object) {
    return fromBits(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(object));
  }

  constexpr uint64_t bits() const { return bits_; }

  constexpr bool isBool() const { return (bits_ | 1) == (QNAN | TAG_TRUE); }
  constexpr bool isNil() const { return bits_ == (QNAN | TAG_NIL); }
  constexpr bool isNumber() const { return (bits_ & QNAN) != QNAN; }
  constexpr bool isObj() const {
    return (bits_ & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT);
  }
  constexpr bool isUninitialized() const {
    return bits_ == (QNAN | TAG_UNINITIALIZED);
  }

  constexpr bool asBool() const { return bits_ == (QNAN | TAG_TRUE); }
  Obj *asObj() const { return (Obj *)(uintptr_t)(bits_ & ~(SIGN_BIT | QNAN)); }

private:
  explicit constexpr Value(uint64_t bits) : bits_(bits) {}

  uint64_t bits_;
};

inline constexpr bool operator==(Value a, Value b) {
  return a.bits() == b.bits();
}

inline constexpr bool operator!=(Value a, Value b) { return !(a == b); }

#define IS_BOOL(value) ((value).isBool())
#define IS_NIL(value) ((value).isNil())
#define IS_NUMBER(value) ((value).isNumber())
#define IS_OBJ(value) ((value).isObj())
#define IS_UNINITIALIZED(value) ((value).isUninitialized())

#define AS_BOOL(value) ((value).asBool())
#define AS_NUMBER(value) valueToNum(value)
#define AS_OBJ(value) ((value).asObj())

#define BOOL_VAL(b) Value::boolean(b)
#define FALSE_VAL Value::fromBits(QNAN | TAG_FALSE)
#define TRUE_VAL Value::fromBits(QNAN | TAG_TRUE)
#define NIL_VAL Value::nil()
#define UNINITIALIZED_VAL Value::uninitialized()
#define NUMBER_VAL(num) numToValue(num)
#define OBJ_VAL(obj) Value::object((Obj *)obj)

static inline double valueToNum(Value value) {
  double num;
  uint64_t bits = value.bits();
  std::memcpy(&num, &bits, sizeof(bits));
  return num;
}

static inline Value numToValue(double num) {
  uint64_t bits;
  std::memcpy(&bits, &num, sizeof(double));
  return Value::fromBits(bits);
}

#else

enum class ValueType : uint8_t {
  Bool,
  Nil,
  Number,
  Obj,
  Uninitialized
};

inline constexpr ValueType VAL_BOOL = ValueType::Bool;
inline constexpr ValueType VAL_NIL = ValueType::Nil;
inline constexpr ValueType VAL_NUMBER = ValueType::Number;
inline constexpr ValueType VAL_OBJ = ValueType::Obj;
inline constexpr ValueType VAL_UNINITIALIZED = ValueType::Uninitialized;

struct Value {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj *obj;
  } as;
};

inline Value boolValue(bool value) {
  Value result;
  result.type = VAL_BOOL;
  result.as.boolean = value;
  return result;
}

inline Value nilValue() {
  Value result;
  result.type = VAL_NIL;
  result.as.number = 0;
  return result;
}

inline Value uninitializedValue() {
  Value result;
  result.type = VAL_UNINITIALIZED;
  result.as.number = 0;
  return result;
}

inline Value numberValue(double value) {
  Value result;
  result.type = VAL_NUMBER;
  result.as.number = value;
  return result;
}

inline Value objectValue(Obj *object) {
  Value result;
  result.type = VAL_OBJ;
  result.as.obj = object;
  return result;
}

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value) ((value).type == VAL_OBJ)
#define IS_UNINITIALIZED(value) ((value).type == VAL_UNINITIALIZED)

#define AS_OBJ(value) ((value).as.obj)
#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

#define BOOL_VAL(value) boolValue(value)
#define NIL_VAL nilValue()
#define UNINITIALIZED_VAL uninitializedValue()
#define NUMBER_VAL(value) numberValue(value)
#define OBJ_VAL(object) objectValue((Obj *)object)

#endif

using ValueArray = std::vector<Value>;

static inline bool valuesEqual(Value a, Value b) {
#ifdef NAN_BOXING
  if (IS_NUMBER(a) && IS_NUMBER(b)) {
    return AS_NUMBER(a) == AS_NUMBER(b);
  }
  return a == b;
#else
  if (a.type != b.type)
    return false;
  switch (a.type) {
  case VAL_BOOL:
    return AS_BOOL(a) == AS_BOOL(b);
  case VAL_NIL:
    return true;
  case VAL_NUMBER:
    return AS_NUMBER(a) == AS_NUMBER(b);
  case VAL_OBJ:
    return AS_OBJ(a) == AS_OBJ(b);
  case VAL_UNINITIALIZED:
    return true;
  default:
    return false;
  }
#endif
}

void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);
void printValue(Value value);

} // namespace cpplox

#endif
