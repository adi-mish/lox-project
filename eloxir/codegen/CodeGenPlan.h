#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eloxir {

class Stmt;

enum class PlannedMethodKind { Empty, FieldGetter };

struct PlannedMethod {
  PlannedMethodKind kind = PlannedMethodKind::Empty;
  int arity = 0;
  std::string fieldName;
  uint32_t fieldSlot = 0;
};

struct PlannedClass {
  int initializerArity = 0;
  bool trivialInitializer = false;
  bool hasSuperclass = false;
  bool linearInitializer = false;
  std::unordered_map<std::string, uint32_t> fieldSlots;
  std::unordered_map<std::string, PlannedMethod> methods;
  std::unordered_set<std::string> omittableMethods;
};

struct PlannedReceiver {
  std::string className;
};

class CodeGenPlan {
public:
  static CodeGenPlan analyze(
      const std::vector<std::unique_ptr<Stmt>> &statements);

  const PlannedClass *findStableClass(const std::string &name) const;
  const PlannedClass *findStableTrivialClass(const std::string &name) const;
  const PlannedReceiver *findStableReceiver(const std::string &name) const;
  const PlannedMethod *findStableReceiverMethod(
      const std::string &receiverName, const std::string &methodName) const;
  bool canOmitMethodObject(const std::string &className,
                           const std::string &methodName) const;

private:
  std::unordered_map<std::string, PlannedClass> stableClasses;
  std::unordered_map<std::string, PlannedReceiver> stableReceivers;
};

} // namespace eloxir
