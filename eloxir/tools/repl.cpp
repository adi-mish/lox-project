#include "../codegen/CodeGenVisitor.h"
#include "../frontend/Parser.h"
#include "../frontend/Resolver.h"
#include "../frontend/CompileError.h"
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

namespace {

enum class ExitCode : int {
  kOk = 0,
  kCompileError = 65,
  kRuntimeError = 70,
};

std::string tokenTypeName(eloxir::TokenType type) {
  using eloxir::TokenType;
  switch (type) {
  case TokenType::LEFT_PAREN:
    return "LEFT_PAREN";
  case TokenType::RIGHT_PAREN:
    return "RIGHT_PAREN";
  case TokenType::LEFT_BRACE:
    return "LEFT_BRACE";
  case TokenType::RIGHT_BRACE:
    return "RIGHT_BRACE";
  case TokenType::COMMA:
    return "COMMA";
  case TokenType::DOT:
    return "DOT";
  case TokenType::MINUS:
    return "MINUS";
  case TokenType::PLUS:
    return "PLUS";
  case TokenType::SEMICOLON:
    return "SEMICOLON";
  case TokenType::SLASH:
    return "SLASH";
  case TokenType::STAR:
    return "STAR";
  case TokenType::BANG:
    return "BANG";
  case TokenType::BANG_EQUAL:
    return "BANG_EQUAL";
  case TokenType::EQUAL:
    return "EQUAL";
  case TokenType::EQUAL_EQUAL:
    return "EQUAL_EQUAL";
  case TokenType::GREATER:
    return "GREATER";
  case TokenType::GREATER_EQUAL:
    return "GREATER_EQUAL";
  case TokenType::LESS:
    return "LESS";
  case TokenType::LESS_EQUAL:
    return "LESS_EQUAL";
  case TokenType::IDENTIFIER:
    return "IDENTIFIER";
  case TokenType::STRING:
    return "STRING";
  case TokenType::NUMBER:
    return "NUMBER";
  case TokenType::AND:
    return "AND";
  case TokenType::CLASS:
    return "CLASS";
  case TokenType::ELSE:
    return "ELSE";
  case TokenType::FALSE:
    return "FALSE";
  case TokenType::FUN:
    return "FUN";
  case TokenType::FOR:
    return "FOR";
  case TokenType::IF:
    return "IF";
  case TokenType::NIL:
    return "NIL";
  case TokenType::OR:
    return "OR";
  case TokenType::PRINT:
    return "PRINT";
  case TokenType::RETURN:
    return "RETURN";
  case TokenType::SUPER:
    return "SUPER";
  case TokenType::THIS:
    return "THIS";
  case TokenType::TRUE:
    return "TRUE";
  case TokenType::VAR:
    return "VAR";
  case TokenType::WHILE:
    return "WHILE";
  case TokenType::EOF_TOKEN:
    return "EOF";
  }
  return "UNKNOWN";
}

std::string literalToString(const eloxir::Token &token) {
  const auto &literal = token.getLiteral();
  if (std::holds_alternative<std::monostate>(literal)) {
    return "null";
  }
  if (const auto *num = std::get_if<double>(&literal)) {
    std::ostringstream out;
    out.precision(15);
    out << *num;
    auto text = out.str();
    if (text.find('e') == std::string::npos && text.find('E') == std::string::npos &&
        text.find('.') == std::string::npos) {
      text += ".0";
    }
    return text;
  }
  if (const auto *str = std::get_if<std::string>(&literal)) {
    return *str;
  }
  if (const auto *boolean = std::get_if<bool>(&literal)) {
    return *boolean ? "true" : "false";
  }
  return "null";
}

int scanFile(const std::string &filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file '" << filename << "'\n";
    return static_cast<int>(ExitCode::kRuntimeError);
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();

  try {
    eloxir::Scanner scanner(source);
    auto tokens = scanner.scanTokens();
    for (const auto &token : tokens) {
      std::cout << tokenTypeName(token.getType()) << ' '
                << token.getLexeme() << ' ' << literalToString(token) << '\n';
    }
    return static_cast<int>(ExitCode::kOk);
  } catch (const std::runtime_error &e) {
    std::cerr << "Scan error: " << e.what() << '\n';
    return static_cast<int>(ExitCode::kCompileError);
  }
}

// Function to parse and execute a file
std::pair<std::vector<std::unique_ptr<eloxir::Stmt>>, std::string>
parseFile(const std::string &source) {
  try {
    eloxir::Scanner scanner(source);
    auto tokens = scanner.scanTokens();
    eloxir::Parser parser(tokens);
    auto stmts = parser.parse();
    if (parser.hadErrors()) {
      return {std::vector<std::unique_ptr<eloxir::Stmt>>{},
              parser.firstErrorMessage()};
    }
    return {std::move(stmts), ""};
  } catch (const std::runtime_error &e) {
    return {std::vector<std::unique_ptr<eloxir::Stmt>>{}, e.what()};
  }
}

int runFile(const std::string &filename) {
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
    return static_cast<int>(ExitCode::kRuntimeError);
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();
  file.close();

  // Parse file
  auto [stmts, error] = parseFile(source);
  if (!error.empty()) {
    std::cerr << "Parse error: " << error << '\n';
    return static_cast<int>(ExitCode::kCompileError);
  }

  if (stmts.empty()) {
    return static_cast<int>(ExitCode::kOk); // Empty file
  }

  // Resolve variables and analyze upvalues
  eloxir::Resolver resolver;
  try {
    resolver.resolve(stmts);
  } catch (const eloxir::CompileError &e) {
    std::cerr << "Resolution error: " << e.what() << '\n';
    return static_cast<int>(ExitCode::kCompileError);
  } catch (const std::runtime_error &e) {
    std::cerr << "Resolution error: " << e.what() << '\n';
    return static_cast<int>(ExitCode::kCompileError);
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
      return static_cast<int>(ExitCode::kCompileError);
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
      return static_cast<int>(ExitCode::kRuntimeError);
    }

  } catch (const eloxir::CompileError &e) {
    std::cerr << "Compile error: " << e.what() << '\n';
    elx_clear_runtime_error();
    return static_cast<int>(ExitCode::kCompileError);
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << '\n';
    elx_clear_runtime_error();
    return static_cast<int>(ExitCode::kRuntimeError);
  } catch (...) {
    std::cerr << "Unknown error occurred\n";
    elx_clear_runtime_error();
    return static_cast<int>(ExitCode::kRuntimeError);
  }

  return static_cast<int>(ExitCode::kOk);
}

} // namespace

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

    } catch (const eloxir::CompileError &e) {
      std::cerr << "Compile error: " << e.what() << '\n';
      elx_clear_runtime_error();
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
    runREPL();
    return 0;
  }

  std::string option = argv[1];
  if (option == "--scan") {
    if (argc != 3) {
      std::cerr << "Usage: " << argv[0] << " --scan <filename>\n";
      return static_cast<int>(ExitCode::kRuntimeError);
    }
    return scanFile(argv[2]);
  }

  if (argc == 2) {
    return runFile(argv[1]);
  }

  std::cerr << "Usage: " << argv[0] << " [--scan <filename>] [filename]\n";
  std::cerr << "  No arguments: Start REPL\n";
  std::cerr << "  --scan <file>: Print tokens produced by scanner\n";
  std::cerr << "  <file>: Execute file\n";
  return static_cast<int>(ExitCode::kRuntimeError);
}
