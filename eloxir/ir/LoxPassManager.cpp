#include "LoxPassManager.h"
#include "LoxIRPrinter.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>

namespace eloxir::loxir {

void LoxPassManager::add(std::unique_ptr<LoxPass> pass) {
  passes_.push_back(std::move(pass));
}

std::vector<PassEvent> LoxPassManager::run(LoxModule &module) const {
  std::vector<PassEvent> events;
  events.reserve(passes_.size());
  for (const auto &pass : passes_) {
    bool changed = pass->run(module);
    events.push_back(PassEvent{pass->name(), changed});
    if (loxirEnvFlag("ELOXIR_TRACE_PASSES")) {
      std::cerr << "eloxir-loxir: pass " << pass->name()
                << (changed ? " changed" : " unchanged") << '\n';
    }
  }
  return events;
}

bool loxirEnvFlag(const char *name) {
  const char *value = std::getenv(name);
  if (!value) {
    return false;
  }
  std::string_view flag(value);
  return flag == "1" || flag == "true" || flag == "TRUE" || flag == "on" ||
         flag == "ON" || flag == "yes" || flag == "YES";
}

void dumpModuleIfRequested(const LoxModule &module, const char *phase) {
  if (loxirEnvFlag("ELOXIR_PRINT_LOXIR")) {
    std::cerr << "\n; ----- eloxir " << phase << " LoxIR: " << module.name()
              << " -----\n"
              << moduleToString(module);
  }

  const char *dir = std::getenv("ELOXIR_DUMP_LOXIR");
  if (!dir || dir[0] == '\0') {
    return;
  }

  std::string path = std::string(dir) + "/" + module.name() + "." + phase +
                     ".loxir";
  std::ofstream out(path);
  if (out) {
    printModule(out, module);
  } else if (loxirEnvFlag("ELOXIR_TRACE_PASSES")) {
    std::cerr << "eloxir-loxir: failed to write " << path << '\n';
  }
}

} // namespace eloxir::loxir
