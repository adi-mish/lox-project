#include "ObjShape.h"

namespace eloxir {

ObjShape::ObjShape(ObjShape *parentShape, ObjString *field)
    : parent(parentShape), addedField(field), slotCount(0) {
  if (parent) {
    slotCount = parent->slotCount;
    fieldOrder = parent->fieldOrder;
    slotCache = parent->slotCache;
  }

  if (field) {
    slotCount += 1;
    fieldOrder.push_back(field);
    slotCache[field] = slotCount - 1;
  }
}

static void destroySubtree(ObjShape *shape) {
  if (!shape) {
    return;
  }
  for (auto &entry : shape->transitions) {
    destroySubtree(entry.second);
  }
  delete shape;
}

ObjShape *createRootShape() { return new ObjShape(nullptr, nullptr); }

ObjShape *shapeEnsureTransition(ObjShape *shape, ObjString *field) {
  if (!shape || !field) {
    return shape;
  }

  auto slotIt = shape->slotCache.find(field);
  if (slotIt != shape->slotCache.end()) {
    return shape;
  }

  auto transIt = shape->transitions.find(field);
  if (transIt != shape->transitions.end()) {
    return transIt->second;
  }

  auto *next = new ObjShape(shape, field);
  shape->transitions[field] = next;
  return next;
}

bool shapeTryGetSlot(const ObjShape *shape, ObjString *field, size_t *outSlot) {
  if (!shape || !field) {
    return false;
  }

  auto it = shape->slotCache.find(field);
  if (it == shape->slotCache.end()) {
    return false;
  }

  if (outSlot) {
    *outSlot = it->second;
  }
  return true;
}

void shapeDestroyTree(ObjShape *shape) { destroySubtree(shape); }

} // namespace eloxir

