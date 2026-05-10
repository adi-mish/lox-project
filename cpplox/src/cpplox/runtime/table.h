#ifndef clox_table_h
#define clox_table_h

#include <vector>

#include "common.h"
#include "value.h"

namespace cpplox {

typedef struct Entry {
  ObjString *key = nullptr;
  Value value = NIL_VAL;
} Entry;

class Table {
public:
  Table() = default;
  Table(const Table &) = delete;
  Table &operator=(const Table &) = delete;

  void clear();

  bool get(ObjString *key, Value *value) const;
  Entry *findSlot(ObjString *key);
  Entry *getEntry(ObjString *key);
  bool set(ObjString *key, Value value);
  bool remove(ObjString *key);
  void addAllFrom(const Table &from);
  ObjString *findString(const char *chars, int length, uint32_t hash) const;
  void removeWhite();
  void mark() const;

  int count() const { return count_; }
  int capacity() const { return static_cast<int>(entries_.size()); }
  uint32_t version() const { return version_; }

private:
  static Entry *findEntry(Entry *entries, int capacity, ObjString *key);
  static const Entry *findEntry(const Entry *entries, int capacity,
                                ObjString *key);
  void adjustCapacity(int capacity);

  int count_ = 0;
  uint32_t version_ = 0;
  std::vector<Entry> entries_;
};

} // namespace cpplox

#endif
