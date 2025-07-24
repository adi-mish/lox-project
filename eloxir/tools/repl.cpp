#include "../codegen/CodeGenVisitor.h"
#include "../frontend/Parser.h"
#include "../jit/EloxirJIT.h"
#include <iostream>
#include <memory>
#include <string>

using namespace llvm;

int main() {
  auto jit = cantFail(eloxir::EloxirJIT::Create());
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
    exprAST->accept(&cg);

    // wrap in `__expr` function returning Value
    auto fnTy = FunctionType::get(cg.llvmValueTy(), {}, false);
    auto fn = Function::Create(fnTy, Function::ExternalLinkage, "__expr", *mod);
    cg.builder.SetInsertPoint(BasicBlock::Create(ctx, "entry", fn));
    exprAST->codegen(cg);
    cg.builder.CreateRet(cg.value);

    cantFail(jit->addModule(orc::ThreadSafeModule(
        std::move(mod), std::make_unique<LLVMContext>())));
    auto sym = cantFail(jit->lookup("__expr"));
    using FnTy = eloxir::Value (*)();
    eloxir::Value result = reinterpret_cast<FnTy>(sym.getAddress())();
    /* do nothing - elx_print already printed */
  }
  return 0;
}
