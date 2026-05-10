#pragma once

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>

namespace eloxir {

llvm::StructType *getOrCreatePropertyCacheEntryIRType(llvm::LLVMContext &ctx);
llvm::StructType *getOrCreatePropertyCacheIRType(llvm::LLVMContext &ctx);
llvm::StructType *getOrCreateCallInlineCacheIRType(llvm::LLVMContext &ctx);

void declareRuntimeBuiltins(llvm::Module &module);

} // namespace eloxir
