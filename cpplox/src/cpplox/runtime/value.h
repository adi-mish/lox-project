#pragma once

#include <cstring>
#include <vector>

#include "common.h"

namespace cpplox {

struct Obj;
struct ObjString;

inline constexpr uint64_t kSignBit = 0x8000000000000000ull;
inline constexpr uint64_t kQNaN = 0x7ffc000000000000ull;
inline constexpr uint64_t kTagNil = 1;
inline constexpr uint64_t kTagFalse = 2;
inline constexpr uint64_t kTagTrue = 3;
inline constexpr uint64_t kTagUninitialized = 4;

class Value {
public:
  constexpr Value() : bits_(kQNaN | kTagNil) {}

  static constexpr Value fromBits(uint64_t bits) { return Value(bits); }
  static constexpr Value boolean(bool value) {
    return fromBits(kQNaN | (value ? kTagTrue : kTagFalse));
  }
  static constexpr Value nil() { return fromBits(kQNaN | kTagNil); }
  static constexpr Value uninitialized() {
    return fromBits(kQNaN | kTagUninitialized);
  }
  static Value object(Obj *object) {
    return fromBits(kSignBit | kQNaN |
                    static_cast<uint64_t>(reinterpret_cast<uintptr_t>(object)));
  }

  constexpr uint64_t bits() const { return bits_; }

  constexpr bool isBool() const { return (bits_ | 1) == (kQNaN | kTagTrue); }
  constexpr bool isNil() const { return bits_ == (kQNaN | kTagNil); }
  constexpr bool isNumber() const { return (bits_ & kQNaN) != kQNaN; }
  constexpr bool isObj() const {
    return (bits_ & (kQNaN | kSignBit)) == (kQNaN | kSignBit);
  }
  constexpr bool isUninitialized() const {
    return bits_ == (kQNaN | kTagUninitialized);
  }

  constexpr bool asBool() const { return bits_ == (kQNaN | kTagTrue); }
  Obj *asObj() const {
    return reinterpret_cast<Obj *>(
        static_cast<uintptr_t>(bits_ & ~(kSignBit | kQNaN)));
  }

private:
  explicit constexpr Value(uint64_t bits) : bits_(bits) {}

  uint64_t bits_;
};

inline constexpr bool operator==(Value a, Value b) {
  return a.bits() == b.bits();
}

inline constexpr bool operator!=(Value a, Value b) { return !(a == b); }

inline bool isBool(Value value) { return value.isBool(); }
inline bool isNil(Value value) { return value.isNil(); }
inline bool isNumber(Value value) { return value.isNumber(); }
inline bool isObj(Value value) { return value.isObj(); }
inline bool isUninitialized(Value value) { return value.isUninitialized(); }

inline bool asBool(Value value) { return value.asBool(); }
inline Obj *asObj(Value value) { return value.asObj(); }

inline Value boolValue(bool value) { return Value::boolean(value); }
inline Value falseValue() { return Value::fromBits(kQNaN | kTagFalse); }
inline Value trueValue() { return Value::fromBits(kQNaN | kTagTrue); }
inline Value nilValue() { return Value::nil(); }
inline Value uninitializedValue() { return Value::uninitialized(); }
inline Value objectValue(Obj *object) { return Value::object(object); }
template <typename Object> inline Value objectValue(Object *object) {
  return Value::object(&object->obj);
}

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

inline double asNumber(Value value) { return valueToNum(value); }
inline Value numberValue(double value) { return numToValue(value); }

using ValueArray = std::vector<Value>;

static inline bool valuesEqual(Value a, Value b) {
  if (isNumber(a) && isNumber(b)) {
    return asNumber(a) == asNumber(b);
  }
  return a == b;
}

void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);
void printValue(Value value);

} // namespace cpplox
