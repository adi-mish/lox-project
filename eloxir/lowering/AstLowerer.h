#pragma once

#include "../frontend/Expr.h"
#include "../frontend/Stmt.h"
#include "../ir/LoxIR.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eloxir::loxir {

class AstLowerer {
public:
  explicit AstLowerer(
      const std::unordered_map<const Expr *, int> *resolvedLocals = nullptr,
      const std::unordered_map<const Function *, std::vector<std::string>>
          *functionUpvalues = nullptr);

  LoxModule lower(const std::string &moduleName,
                  const std::vector<std::unique_ptr<Stmt>> &statements);

private:
  const std::unordered_map<const Expr *, int> *resolvedLocals_;
  const std::unordered_map<const Function *, std::vector<std::string>>
      *functionUpvalues_;
};

} // namespace eloxir::loxir
