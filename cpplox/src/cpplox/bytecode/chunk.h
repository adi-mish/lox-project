#pragma once

#include <vector>

#include "common.h"
#include "value.h"

namespace cpplox {

struct Entry;
struct ObjClass;

enum class InlineCacheKind : uint8_t {
  Empty,
  Global,
  Field,
  Method,
};

inline constexpr InlineCacheKind CACHE_EMPTY = InlineCacheKind::Empty;
inline constexpr InlineCacheKind CACHE_GLOBAL = InlineCacheKind::Global;
inline constexpr InlineCacheKind CACHE_FIELD = InlineCacheKind::Field;
inline constexpr InlineCacheKind CACHE_METHOD = InlineCacheKind::Method;

struct InlineCache {
  InlineCacheKind kind = CACHE_EMPTY;
  ObjString *key = nullptr;
  Entry *entry = nullptr;
  uint32_t tableVersion = 0;
  ObjClass *ownerClass = nullptr;
  uint32_t secondaryVersion = 0;
  int entryIndex = -1;
  Value value = nilValue();
};

enum class Opcode : uint8_t {
  Constant,
  Constant0,
  Constant1,
  Constant2,
  Constant3,
  Constant4,
  Constant5,
  Constant6,
  Constant7,
  Nil,
  True,
  False,
  Pop,
  GetLocal,
  GetLocal0,
  GetLocal1,
  GetLocal2,
  GetLocal3,
  GetLocal4,
  GetLocal5,
  GetLocal6,
  GetLocal7,
  SetLocal,
  SetLocal0,
  SetLocal1,
  SetLocal2,
  SetLocal3,
  SetLocal4,
  SetLocal5,
  SetLocal6,
  SetLocal7,
  GetGlobal,
  DefineGlobal,
  SetGlobal,
  GetUpvalue,
  SetUpvalue,
  GetProperty,
  SetProperty,
  GetSuper,
  Equal,
  Greater,
  Less,
  Add,
  Subtract,
  Multiply,
  Divide,
  Not,
  Negate,
  Print,
  Jump,
  JumpIfFalse,
  Loop,
  Call,
  Invoke,
  SuperInvoke,
  Closure,
  CloseUpvalue,
  Return,
  Class,
  Inherit,
  Method,
  Count
};

inline constexpr uint8_t opcodeByte(Opcode opcode) {
  return static_cast<uint8_t>(opcode);
}

inline constexpr uint8_t OP_CONSTANT = opcodeByte(Opcode::Constant);
inline constexpr uint8_t OP_CONSTANT_0 = opcodeByte(Opcode::Constant0);
inline constexpr uint8_t OP_CONSTANT_1 = opcodeByte(Opcode::Constant1);
inline constexpr uint8_t OP_CONSTANT_2 = opcodeByte(Opcode::Constant2);
inline constexpr uint8_t OP_CONSTANT_3 = opcodeByte(Opcode::Constant3);
inline constexpr uint8_t OP_CONSTANT_4 = opcodeByte(Opcode::Constant4);
inline constexpr uint8_t OP_CONSTANT_5 = opcodeByte(Opcode::Constant5);
inline constexpr uint8_t OP_CONSTANT_6 = opcodeByte(Opcode::Constant6);
inline constexpr uint8_t OP_CONSTANT_7 = opcodeByte(Opcode::Constant7);
inline constexpr uint8_t OP_NIL = opcodeByte(Opcode::Nil);
inline constexpr uint8_t OP_TRUE = opcodeByte(Opcode::True);
inline constexpr uint8_t OP_FALSE = opcodeByte(Opcode::False);
inline constexpr uint8_t OP_POP = opcodeByte(Opcode::Pop);
inline constexpr uint8_t OP_GET_LOCAL = opcodeByte(Opcode::GetLocal);
inline constexpr uint8_t OP_GET_LOCAL_0 = opcodeByte(Opcode::GetLocal0);
inline constexpr uint8_t OP_GET_LOCAL_1 = opcodeByte(Opcode::GetLocal1);
inline constexpr uint8_t OP_GET_LOCAL_2 = opcodeByte(Opcode::GetLocal2);
inline constexpr uint8_t OP_GET_LOCAL_3 = opcodeByte(Opcode::GetLocal3);
inline constexpr uint8_t OP_GET_LOCAL_4 = opcodeByte(Opcode::GetLocal4);
inline constexpr uint8_t OP_GET_LOCAL_5 = opcodeByte(Opcode::GetLocal5);
inline constexpr uint8_t OP_GET_LOCAL_6 = opcodeByte(Opcode::GetLocal6);
inline constexpr uint8_t OP_GET_LOCAL_7 = opcodeByte(Opcode::GetLocal7);
inline constexpr uint8_t OP_SET_LOCAL = opcodeByte(Opcode::SetLocal);
inline constexpr uint8_t OP_SET_LOCAL_0 = opcodeByte(Opcode::SetLocal0);
inline constexpr uint8_t OP_SET_LOCAL_1 = opcodeByte(Opcode::SetLocal1);
inline constexpr uint8_t OP_SET_LOCAL_2 = opcodeByte(Opcode::SetLocal2);
inline constexpr uint8_t OP_SET_LOCAL_3 = opcodeByte(Opcode::SetLocal3);
inline constexpr uint8_t OP_SET_LOCAL_4 = opcodeByte(Opcode::SetLocal4);
inline constexpr uint8_t OP_SET_LOCAL_5 = opcodeByte(Opcode::SetLocal5);
inline constexpr uint8_t OP_SET_LOCAL_6 = opcodeByte(Opcode::SetLocal6);
inline constexpr uint8_t OP_SET_LOCAL_7 = opcodeByte(Opcode::SetLocal7);
inline constexpr uint8_t OP_GET_GLOBAL = opcodeByte(Opcode::GetGlobal);
inline constexpr uint8_t OP_DEFINE_GLOBAL = opcodeByte(Opcode::DefineGlobal);
inline constexpr uint8_t OP_SET_GLOBAL = opcodeByte(Opcode::SetGlobal);
inline constexpr uint8_t OP_GET_UPVALUE = opcodeByte(Opcode::GetUpvalue);
inline constexpr uint8_t OP_SET_UPVALUE = opcodeByte(Opcode::SetUpvalue);
inline constexpr uint8_t OP_GET_PROPERTY = opcodeByte(Opcode::GetProperty);
inline constexpr uint8_t OP_SET_PROPERTY = opcodeByte(Opcode::SetProperty);
inline constexpr uint8_t OP_GET_SUPER = opcodeByte(Opcode::GetSuper);
inline constexpr uint8_t OP_EQUAL = opcodeByte(Opcode::Equal);
inline constexpr uint8_t OP_GREATER = opcodeByte(Opcode::Greater);
inline constexpr uint8_t OP_LESS = opcodeByte(Opcode::Less);
inline constexpr uint8_t OP_ADD = opcodeByte(Opcode::Add);
inline constexpr uint8_t OP_SUBTRACT = opcodeByte(Opcode::Subtract);
inline constexpr uint8_t OP_MULTIPLY = opcodeByte(Opcode::Multiply);
inline constexpr uint8_t OP_DIVIDE = opcodeByte(Opcode::Divide);
inline constexpr uint8_t OP_NOT = opcodeByte(Opcode::Not);
inline constexpr uint8_t OP_NEGATE = opcodeByte(Opcode::Negate);
inline constexpr uint8_t OP_PRINT = opcodeByte(Opcode::Print);
inline constexpr uint8_t OP_JUMP = opcodeByte(Opcode::Jump);
inline constexpr uint8_t OP_JUMP_IF_FALSE = opcodeByte(Opcode::JumpIfFalse);
inline constexpr uint8_t OP_LOOP = opcodeByte(Opcode::Loop);
inline constexpr uint8_t OP_CALL = opcodeByte(Opcode::Call);
inline constexpr uint8_t OP_INVOKE = opcodeByte(Opcode::Invoke);
inline constexpr uint8_t OP_SUPER_INVOKE = opcodeByte(Opcode::SuperInvoke);
inline constexpr uint8_t OP_CLOSURE = opcodeByte(Opcode::Closure);
inline constexpr uint8_t OP_CLOSE_UPVALUE = opcodeByte(Opcode::CloseUpvalue);
inline constexpr uint8_t OP_RETURN = opcodeByte(Opcode::Return);
inline constexpr uint8_t OP_CLASS = opcodeByte(Opcode::Class);
inline constexpr uint8_t OP_INHERIT = opcodeByte(Opcode::Inherit);
inline constexpr uint8_t OP_METHOD = opcodeByte(Opcode::Method);
inline constexpr int OP_COUNT = static_cast<int>(Opcode::Count);

class Chunk {
public:
  int size() const { return static_cast<int>(code_.size()); }
  bool empty() const { return code_.empty(); }

  uint8_t *codeData() { return code_.data(); }
  const uint8_t *codeData() const { return code_.data(); }
  uint8_t byteAt(int offset) const { return code_[offset]; }
  uint8_t &byteAt(int offset) { return code_[offset]; }
  int lineAt(size_t offset) const { return lines_[offset]; }

  void write(uint8_t byte, int line);
  void truncate(int size);
  int addConstant(Value value);

  Value constantAt(int index) const { return constants_[index]; }
  Value *constantsData() { return constants_.data(); }
  const ValueArray &constants() const { return constants_; }
  ValueArray &constants() { return constants_; }

  InlineCache &inlineCache(int index) { return inlineCaches_[index]; }
  const std::vector<InlineCache> &inlineCaches() const { return inlineCaches_; }
  std::vector<InlineCache> &inlineCaches() { return inlineCaches_; }

private:
  std::vector<uint8_t> code_;
  std::vector<int> lines_;
  std::vector<InlineCache> inlineCaches_;
  ValueArray constants_;
};

void writeChunk(Chunk *chunk, uint8_t byte, int line);
int addConstant(Chunk *chunk, Value value);

} // namespace cpplox
