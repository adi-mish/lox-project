#pragma once

#include "LoxIR.h"

#include <memory>
#include <string>
#include <vector>

namespace eloxir::loxir {

class LoxPass {
public:
  virtual ~LoxPass() = default;
  virtual std::string name() const = 0;
  virtual bool run(LoxModule &module) = 0;
};

struct PassEvent {
  std::string name;
  bool changed = false;
};

class LoxPassManager {
public:
  void add(std::unique_ptr<LoxPass> pass);
  std::vector<PassEvent> run(LoxModule &module) const;
  bool empty() const { return passes_.empty(); }

private:
  std::vector<std::unique_ptr<LoxPass>> passes_;
};

bool loxirEnvFlag(const char *name);
void dumpModuleIfRequested(const LoxModule &module, const char *phase);

} // namespace eloxir::loxir
