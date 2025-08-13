#include "../codegen/CodeGenVisitor.h"
#include "../frontend/Parser.h"
#include "../jit/EloxirJIT.h"
#include "../runtime/RuntimeAPI.h"
#include <iostream>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <memory>
#include <string>

using namespace llvm;

int main() {
  // Initialize LLVM targets
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  // Initialize runtime global state
  elx_initialize_global_builtins();

  auto jit = cantFail(eloxir::EloxirJIT::Create());
  unsigned int lineCount = 0; // Add counter for unique function names

  std::cout << "Eloxir REPL - Enter 'exit' to quit\n";
  std::cout << "Variables and functions defined here persist across lines.\n";

  while (true) {
    std::cout << ">>> ";
    std::string line;
    if (!std::getline(std::cin, line))
      break;
    if (line == "exit")
      break;
    if (line.empty())
      continue;

    // Clear any previous runtime errors
    elx_clear_runtime_error();

    auto [exprAST, errors] = eloxir::parseREPL(line); // wraps in implicit print
    if (!errors.empty()) {
      std::cerr << "Parse error: " << errors << '\n';
      continue;
    }
    if (!exprAST)
      continue;

    try {
      // Create a new context and module for each line
      LLVMContext lineCtx;
      auto lineMod = std::make_unique<Module>("repl_line", lineCtx);
      eloxir::CodeGenVisitor lineCG(*lineMod);

      // wrap in unique function name to avoid duplicates
      std::string fnName = "__expr" + std::to_string(lineCount++);
      auto fnTy = FunctionType::get(lineCG.llvmValueTy(), {}, false);
      auto fn =
          Function::Create(fnTy, Function::ExternalLinkage, fnName, *lineMod);
      lineCG.getBuilder().SetInsertPoint(
          BasicBlock::Create(lineCtx, "entry", fn));

      // Generate code for the statement/expression
      exprAST->codegen(lineCG);
      lineCG.getBuilder().CreateRet(lineCG.value);

      // Verify the function before executing
      if (llvm::verifyFunction(*fn, &llvm::errs())) {
        std::cerr << "Generated invalid LLVM IR. Skipping execution.\n";
        continue;
      }

      cantFail(jit->addModule(orc::ThreadSafeModule(
          std::move(lineMod), std::make_unique<LLVMContext>())));

      auto sym = cantFail(jit->lookup(fnName));
      using FnTy = eloxir::Value (*)();
      reinterpret_cast<FnTy>(sym.getAddress())();

      // Check for runtime errors after execution
      if (elx_has_runtime_error()) {
        // Error already printed by runtime, just clear it
        elx_clear_runtime_error();
      }

    } catch (const std::exception &e) {
      std::cerr << "Error: " << e.what() << '\n';
      elx_clear_runtime_error();
    } catch (...) {
      std::cerr << "Unknown error occurred\n";
      elx_clear_runtime_error();
    }

    // Clean up temporary allocations between REPL iterations
    // This preserves global state while cleaning up temporary objects
    elx_cleanup_all_objects();
  }

  std::cout << "Goodbye!\n";
  return 0;
}
