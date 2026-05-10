#pragma once

#include "../ir/LoxPassManager.h"

#include <memory>

namespace eloxir::loxir {

std::unique_ptr<LoxPass> createConstantFoldingPass();
std::unique_ptr<LoxPass> createDeadCodeEliminationPass();
LoxPassManager createDefaultLoxPassPipeline();

} // namespace eloxir::loxir
