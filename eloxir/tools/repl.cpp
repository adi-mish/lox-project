#include "../codegen/CodeGenVisitor.h"
#include "../frontend/Parser.h"
#include "../jit/EloxirJIT.h"
#include <iostream>
#include <llvm/Support/TargetSelect.h>
#include <memory>
#include <string>

using namespace llvm;

int main() {
  // Initialize LLVM targets
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  auto jit = cantFail(eloxir::EloxirJIT::Create());
  unsigned int lineCount = 0; // Add counter for unique function names

  while (true) {
    std::cout << ">>> ";
    std::string line;
    if (!std::getline(std::cin, line))
      break;
    if (line == "exit")
      break;

    auto [exprAST, errors] = eloxir::parseREPL(line); // wraps in implicit print
    if (!errors.empty()) {
      std::cerr << errors << '\n';
      continue;
    }
    if (!exprAST)
      continue;

    LLVMContext ctx;
    auto mod = std::make_unique<Module>("repl", ctx);
    eloxir::CodeGenVisitor cg(*mod);

    // wrap in unique function name to avoid duplicates
    std::string fnName = "__expr" + std::to_string(lineCount++);
    auto fnTy = FunctionType::get(cg.llvmValueTy(), {}, false);
    auto fn = Function::Create(fnTy, Function::ExternalLinkage, fnName, *mod);
    cg.getBuilder().SetInsertPoint(BasicBlock::Create(ctx, "entry", fn));
    exprAST->codegen(cg);
    cg.getBuilder().CreateRet(cg.value);

    cantFail(jit->addModule(orc::ThreadSafeModule(
        std::move(mod), std::make_unique<LLVMContext>())));
    auto sym = cantFail(jit->lookup(fnName));
    using FnTy = eloxir::Value (*)();
    eloxir::Value result = reinterpret_cast<FnTy>(sym.getAddress())();
    /* do nothing - elx_print already printed */
  }
  return 0;
}
