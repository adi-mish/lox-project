#pragma once
#include <cstdint>
#include <cstring>

namespace eloxir {

// IEEE‑754 NaN box:
//   64‑bit slot: quiet‑NaN     0xFFF8'0000'0000'0000
//   Top 3 bits encode tag, lower 48 bits encode pointer / payload.
enum class Tag : uint8_t { NUMBER = 0, BOOL = 1, NIL = 2, OBJ = 3 };

class Value {
  uint64_t bits;
  static constexpr uint64_t MASK_TAG = 0x7ULL << 48;
  static constexpr uint64_t QNAN = 0x7ff8000000000000ULL;

public:
  /* ctors for each dynamic type … */
  static Value number(double d) {
    uint64_t u;
    std::memcpy(&u, &d, 8);
    return Value{u};
  }
  static Value boolean(bool b) {
    return Value{QNAN | (static_cast<uint64_t>(Tag::BOOL) << 48) | b};
  }
  static Value nil() {
    return Value{QNAN | (static_cast<uint64_t>(Tag::NIL) << 48)};
  }
  static Value object(void *p) {
    return Value{QNAN | (static_cast<uint64_t>(Tag::OBJ) << 48) |
                 reinterpret_cast<uint64_t>(p)};
  }
  static Value fromBits(uint64_t bits) { return Value{bits}; }

  Tag tag() const {
    // If it's not a special NaN pattern, it's a regular number
    if ((bits & 0xfff0000000000000ULL) != 0x7ff0000000000000ULL) {
      return Tag::NUMBER;
    }
    // If it's a NaN, check if it matches our QNAN pattern
    if ((bits & 0xfff8000000000000ULL) == 0x7ff8000000000000ULL) {
      // Extract just the tag bits (48-50)
      return static_cast<Tag>((bits >> 48) & 0x7);
    }
    // Other NaN patterns are treated as numbers
    return Tag::NUMBER;
  }
  bool isNum() const { return tag() == Tag::NUMBER; }
  double asNum() const {
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
  }
  bool isBool() const { return tag() == Tag::BOOL; }
  bool asBool() const { return bits & 1; }
  bool isNil() const { return tag() == Tag::NIL; }
  bool isObj() const { return tag() == Tag::OBJ; }
  void *asObj() const {
    // Extract the lower 48 bits as the pointer
    return reinterpret_cast<void *>(bits & 0xFFFFFFFFFFFFULL);
  }

  uint64_t getBits() const { return bits; }

private:
  explicit Value(uint64_t b) : bits(b) {}
};

} // namespace eloxir
