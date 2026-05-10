#include <cstring>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

namespace cpplox {

inline constexpr double TABLE_MAX_LOAD = 0.75;

void Table::clear() {
  count_ = 0;
  version_ = 0;
  entries_.clear();
  entries_.shrink_to_fit();
}

Entry *Table::findEntry(Entry *entries, int capacity, ObjString *key) {
  uint32_t index = key->hash & (capacity - 1);
  Entry *tombstone = NULL;

  for (;;) {
    Entry *entry = &entries[index];

    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {

        return tombstone != NULL ? tombstone : entry;
      } else {

        if (tombstone == NULL)
          tombstone = entry;
      }
    } else if (entry->key == key) {

      return entry;
    }

    index = (index + 1) & (capacity - 1);
  }
}

const Entry *Table::findEntry(const Entry *entries, int capacity,
                              ObjString *key) {
  return findEntry(const_cast<Entry *>(entries), capacity, key);
}

bool Table::get(ObjString *key, Value *value) const {
  if (count_ == 0)
    return false;

  const Entry *entry = findEntry(entries_.data(), capacity(), key);
  if (entry->key == NULL)
    return false;

  *value = entry->value;
  return true;
}
Entry *Table::findSlot(ObjString *key) {
  if (entries_.empty())
    return NULL;

  return findEntry(entries_.data(), capacity(), key);
}
Entry *Table::getEntry(ObjString *key) {
  Entry *entry = findSlot(key);
  if (entry == NULL)
    return NULL;
  if (entry->key == NULL)
    return NULL;
  return entry;
}

void Table::adjustCapacity(int capacity) {
  std::vector<Entry> entries(static_cast<size_t>(capacity));

  count_ = 0;
  for (Entry &oldEntry : entries_) {
    Entry *entry = &oldEntry;
    if (entry->key == NULL)
      continue;

    Entry *dest = findEntry(entries.data(), capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    count_++;
  }

  entries_.swap(entries);
  version_++;
}
bool Table::set(ObjString *key, Value value) {
  if (count_ + 1 > capacity() * TABLE_MAX_LOAD) {
    int newCapacity = growCapacity(capacity());
    adjustCapacity(newCapacity);
  }

  Entry *entry = findEntry(entries_.data(), capacity(), key);
  bool isNewKey = entry->key == NULL;

  if (isNewKey && IS_NIL(entry->value)) {
    count_++;
    version_++;
  }

  entry->key = key;
  entry->value = value;
  return isNewKey;
}
bool Table::remove(ObjString *key) {
  if (count_ == 0)
    return false;

  Entry *entry = findEntry(entries_.data(), capacity(), key);
  if (entry->key == NULL)
    return false;

  entry->key = NULL;
  entry->value = BOOL_VAL(true);
  version_++;
  return true;
}
void Table::addAllFrom(const Table &from) {
  for (const Entry &oldEntry : from.entries_) {
    const Entry *entry = &oldEntry;
    if (entry->key != NULL) {
      set(entry->key, entry->value);
    }
  }
}
ObjString *Table::findString(const char *chars, int length,
                             uint32_t hash) const {
  if (count_ == 0)
    return NULL;

  uint32_t index = hash & (capacity() - 1);
  for (;;) {
    const Entry *entry = &entries_[index];
    if (entry->key == NULL) {

      if (IS_NIL(entry->value))
        return NULL;
    } else if (entry->key->length == length && entry->key->hash == hash &&
               memcmp(entry->key->chars, chars, length) == 0) {

      return entry->key;
    }

    index = (index + 1) & (capacity() - 1);
  }
}
void Table::removeWhite() {
  for (Entry &oldEntry : entries_) {
    Entry *entry = &oldEntry;
    if (entry->key != NULL && !entry->key->obj.isMarked) {
      remove(entry->key);
    }
  }
}
void Table::mark() const {
  for (const Entry &entry : entries_) {
    markObject((Obj *)entry.key);
    markValue(entry.value);
  }
}

} // namespace cpplox
