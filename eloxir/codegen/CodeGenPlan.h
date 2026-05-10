#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eloxir {

class Stmt;

struct PlannedClass {
  int initializerArity = 0;
  bool trivialInitializer = false;
};

class CodeGenPlan {
public:
  static CodeGenPlan analyze(
      const std::vector<std::unique_ptr<Stmt>> &statements);

  const PlannedClass *findStableTrivialClass(const std::string &name) const;

private:
  std::unordered_map<std::string, PlannedClass> stableClasses;
};

} // namespace eloxir
