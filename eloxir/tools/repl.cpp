#include "../codegen/CodeGenVisitor.h"
#include "../frontend/Parser.h"
#include "../frontend/Resolver.h"
#include "../frontend/Scanner.h"
#include "../jit/EloxirJIT.h"
#include "../runtime/RuntimeAPI.h"
#include <fstream>
#include <iostream>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <memory>
#include <sstream>
#include <string>

using namespace llvm;

// Function to parse and execute a file
std::pair<std::vector<std::unique_ptr<eloxir::Stmt>>, std::string>
parseFile(const std::string &source) {
  try {
    eloxir::Scanner scanner(source);
    auto tokens = scanner.scanTokens();
    eloxir::Parser parser(tokens);
    auto stmts = parser.parse();
    return {std::move(stmts), ""};
  } catch (const std::runtime_error &e) {
    return {std::vector<std::unique_ptr<eloxir::Stmt>>{}, e.what()};
  }
}

void runFile(const std::string &filename) {
  // Initialize LLVM targets
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  // Initialize runtime global state
  elx_initialize_global_builtins();

  // Read file
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file '" << filename << "'\n";
    return;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();
  file.close();

  // Parse file
  auto [stmts, error] = parseFile(source);
  if (!error.empty()) {
    std::cerr << "Parse error: " << error << '\n';
    return;
  }

  if (stmts.empty()) {
    return; // Empty file
  }

  // Resolve variables and analyze upvalues
  eloxir::Resolver resolver;
  try {
    resolver.resolve(stmts);
  } catch (const std::runtime_error &e) {
    std::cerr << "Resolution error: " << e.what() << '\n';
    return;
  }

  // Clear any previous runtime errors
  elx_clear_runtime_error();

  try {
    auto jit = cantFail(eloxir::EloxirJIT::Create());

    // Create context and module for the entire file
    LLVMContext fileCtx;
    auto fileMod = std::make_unique<Module>("file_module", fileCtx);
    eloxir::CodeGenVisitor fileCG(*fileMod);

    // Pass resolver upvalue information to code generator
    fileCG.setResolverUpvalues(&resolver.function_upvalues);

    // Create main function
    auto fnTy = FunctionType::get(fileCG.llvmValueTy(), {}, false);
    auto fn =
        Function::Create(fnTy, Function::ExternalLinkage, "main", *fileMod);
    fileCG.getBuilder().SetInsertPoint(
        BasicBlock::Create(fileCtx, "entry", fn));

    // Generate code for all statements with two-pass approach for functions
    llvm::Value *lastValue = nullptr;

    // Create a nil literal to get a nil constant
    auto nilLiteral = std::make_unique<eloxir::Literal>(std::monostate{});
    nilLiteral->accept(&fileCG);
    lastValue = fileCG.value; // This will be nil

    // Pass 1: Declare all function signatures
    for (auto &stmt : stmts) {
      if (auto funcStmt = dynamic_cast<eloxir::Function *>(stmt.get())) {
        fileCG.declareFunctionSignature(funcStmt);
      }
    }

    // Pass 2: Process all statements (including function bodies)
    for (auto &stmt : stmts) {
      stmt->accept(&fileCG);
      if (fileCG.value != nullptr) {
        lastValue = fileCG.value;
      }
    }

    // Return the last value (or nil if no expression result)
    fileCG.getBuilder().CreateRet(lastValue);

    // Verify the function before executing
    if (llvm::verifyFunction(*fn, &llvm::errs())) {
      std::cerr << "Generated invalid LLVM IR. Cannot execute.\n";
      return;
    }

    // After the main function is complete, create function objects at global
    // scope The builder is now outside any function
    fileCG.createGlobalFunctionObjects();

    cantFail(jit->addModule(orc::ThreadSafeModule(
        std::move(fileMod), std::make_unique<LLVMContext>())));

    // First, run the global initialization function if it exists
    auto initSymOpt = jit->lookup("__global_init");
    if (initSymOpt) {
      using InitFnTy = void (*)();
      reinterpret_cast<InitFnTy>(initSymOpt->getAddress())();
    }

    auto sym = cantFail(jit->lookup("main"));
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
}

void runREPL() {
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

    // Resolve the statement
    eloxir::Resolver resolver;
    try {
      std::vector<std::unique_ptr<eloxir::Stmt>> stmts;
      stmts.push_back(std::move(exprAST));
      resolver.resolve(stmts);
      exprAST = std::move(stmts[0]);
    } catch (const std::runtime_error &e) {
      std::cerr << "Resolution error: " << e.what() << '\n';
      continue;
    }

    try {
      // Create a new context and module for each line
      LLVMContext lineCtx;
      auto lineMod = std::make_unique<Module>("repl_line", lineCtx);
      eloxir::CodeGenVisitor lineCG(*lineMod);

      // Pass resolver upvalue information to code generator
      lineCG.setResolverUpvalues(&resolver.function_upvalues);

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
}

int main(int argc, char *argv[]) {
  if (argc == 1) {
    // No arguments - run REPL
    runREPL();
  } else if (argc == 2) {
    // One argument - run file
    runFile(argv[1]);
  } else {
    std::cerr << "Usage: " << argv[0] << " [filename]\n";
    std::cerr << "  No arguments: Start REPL\n";
    std::cerr << "  One argument: Execute file\n";
    return 1;
  }

  return 0;
}
