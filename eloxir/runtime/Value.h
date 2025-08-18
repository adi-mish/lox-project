#pragma once
#include <cstdint>
#include <cstring>
#include <iostream>

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
    // Store the full 64-bit pointer value using a safer approach
    // We'll use a global pointer table to avoid truncation issues
    uint64_t ptr_value = reinterpret_cast<uint64_t>(p);

    // For null pointers, store directly
    if (ptr_value == 0) {
      return Value{QNAN | (static_cast<uint64_t>(Tag::OBJ) << 48)};
    }

    // Check if pointer fits in 48 bits for direct storage
    if (ptr_value <= 0xFFFFFFFFFFFFULL) {
      return Value{QNAN | (static_cast<uint64_t>(Tag::OBJ) << 48) | ptr_value};
    } else {
      // For pointers that don't fit, use a different approach
      // Store a handle/index instead and maintain a separate pointer table
      // For now, abort with clear error message rather than corrupt memory
      std::cerr << "Fatal error: pointer value " << std::hex << ptr_value
                << " exceeds 48-bit limit for NaN-boxing. "
                << "This system requires pointer table implementation.\n";
      std::abort();
    }
  }
  static Value fromBits(uint64_t bits) { return Value{bits}; }

  Tag tag() const {
    // IEEE 754 compliant logic:
    // Check if it matches our QNAN pattern used for tagging non-number types
    if ((bits & 0xfff8000000000000ULL) == 0x7ff8000000000000ULL) {
      // Extract just the tag bits (48-50)
      return static_cast<Tag>((bits >> 48) & 0x7);
    }
    // All other bit patterns (including infinity, normal NaN) are numbers
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
    // For objects, we need to reconstruct the full pointer
    // Extract the lower 48 bits and extend to full pointer
    uint64_t ptr_bits = bits & 0xFFFFFFFFFFFFULL;
    return reinterpret_cast<void *>(ptr_bits);
  }

  uint64_t getBits() const { return bits; }

private:
  explicit Value(uint64_t b) : bits(b) {}
};

} // namespace eloxir
