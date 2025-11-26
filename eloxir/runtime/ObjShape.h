#pragma once

#include <cstddef>
#include <vector>
#include <llvm/ADT/DenseMap.h>

namespace eloxir {

struct ObjString;

struct ObjShape {
  ObjShape *parent;
  ObjString *addedField;
  size_t slotCount;
  std::vector<ObjString *> fieldOrder;
  llvm::DenseMap<ObjString *, size_t> slotCache;
  llvm::DenseMap<ObjString *, ObjShape *> transitions;

  ObjShape(ObjShape *parent, ObjString *field);

  size_t fieldCount() const { return slotCount; }
};

ObjShape *createRootShape();
ObjShape *shapeEnsureTransition(ObjShape *shape, ObjString *field);
bool shapeTryGetSlot(const ObjShape *shape, ObjString *field, size_t *outSlot);
void shapeDestroyTree(ObjShape *shape);

} // namespace eloxir

