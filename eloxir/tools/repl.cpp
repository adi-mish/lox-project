#include "../codegen/CodeGenVisitor.h"
#include "../codegen/CodeGenPlan.h"
#include "../frontend/CompileError.h"
#include "../frontend/Parser.h"
#include "../frontend/Resolver.h"
#include "../frontend/Scanner.h"
#include "../jit/EloxirJIT.h"
#include "../jit/OptimisationPipeline.h"
#include "../lowering/AstLowerer.h"
#include "../ir/LoxIRVerifier.h"
#include "../ir/LoxPassManager.h"
#include "../llvm/LoxIRCodeGen.h"
#include "../opt/LoxOptimizationPasses.h"
#include "../runtime/RuntimeAPI.h"
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <cstdlib>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

using namespace llvm;

namespace {

orc::ThreadSafeModule
makeThreadSafeModule(std::unique_ptr<Module> module,
                     std::unique_ptr<LLVMContext> context) {
  return orc::ThreadSafeModule(std::move(module), std::move(context));
}

enum class ExitCode : int {
  kOk = 0,
  kCompileError = 65,
  kRuntimeError = 70,
};

bool hasOversizedLoxIRBlock(const eloxir::loxir::LoxModule &module) {
  static constexpr std::size_t kMaxLoopBodyInstructions = 65535;
  static constexpr std::size_t kConservativeLoxIRInstructionLimit =
      kMaxLoopBodyInstructions / 2;
  for (const auto &function : module.functions()) {
    for (const auto &block : function.blocks()) {
      if (block.instructions().size() > kConservativeLoxIRInstructionLimit) {
        return true;
      }
    }
  }
  return false;
}

class AstPrinter : public eloxir::ExprVisitor {
public:
  std::string print(const eloxir::Expr *expr) {
    if (!expr)
      return "";
    const_cast<eloxir::Expr *>(expr)->accept(this);
    return result;
  }

  void visitBinaryExpr(eloxir::Binary *expr) override {
    result = parenthesize(expr->op.getLexeme(),
                          {expr->left.get(), expr->right.get()});
  }

  void visitGroupingExpr(eloxir::Grouping *expr) override {
    result = parenthesize("group", {expr->expression.get()});
  }

  void visitLiteralExpr(eloxir::Literal *expr) override {
    result = formatLiteral(expr->value);
  }

  void visitUnaryExpr(eloxir::Unary *expr) override {
    result = parenthesize(expr->op.getLexeme(), {expr->right.get()});
  }

  void visitVariableExpr(eloxir::Variable *expr) override {
    result = expr->name.getLexeme();
  }

  void visitAssignExpr(eloxir::Assign *expr) override {
    result = parenthesizeParts("=", expr->name.getLexeme(), expr->value.get());
  }

  void visitLogicalExpr(eloxir::Logical *expr) override {
    result = parenthesize(expr->op.getLexeme(),
                          {expr->left.get(), expr->right.get()});
  }

  void visitCallExpr(eloxir::Call *expr) override {
    result = parenthesizeParts("call", expr->callee.get(), expr->arguments);
  }

  void visitGetExpr(eloxir::Get *expr) override {
    result = parenthesizeParts(".", expr->object.get(), expr->name.getLexeme());
  }

  void visitSetExpr(eloxir::Set *expr) override {
    result = parenthesizeParts("=", expr->object.get(), expr->name.getLexeme(),
                               expr->value.get());
  }

  void visitThisExpr(eloxir::This *) override { result = "this"; }

  void visitSuperExpr(eloxir::Super *expr) override {
    result = parenthesizeParts("super", expr->method.getLexeme());
  }

private:
  std::string result;

  static std::string formatLiteral(const eloxir::Expr::Value &value) {
    if (std::holds_alternative<std::monostate>(value))
      return "nil";
    if (const auto *num = std::get_if<double>(&value)) {
      std::ostringstream out;
      out.precision(15);
      out << *num;
      auto text = out.str();
      if (text.find('e') == std::string::npos &&
          text.find('E') == std::string::npos &&
          text.find('.') == std::string::npos) {
        text += ".0";
      }
      return text;
    }
    if (const auto *str = std::get_if<std::string>(&value))
      return *str;
    if (const auto *boolean = std::get_if<bool>(&value))
      return *boolean ? "true" : "false";
    return "nil";
  }

  std::string parenthesize(const std::string &name,
                           std::initializer_list<const eloxir::Expr *> exprs) {
    std::string builder = "(" + name;
    for (const auto *expr : exprs) {
      builder.push_back(' ');
      builder += print(expr);
    }
    builder.push_back(')');
    return builder;
  }

  template <typename... Parts>
  std::string parenthesizeParts(const std::string &name, Parts &&...parts) {
    std::string builder = "(" + name;
    (appendPart(builder, std::forward<Parts>(parts)), ...);
    builder.push_back(')');
    return builder;
  }

  void appendPart(std::string &builder, const eloxir::Expr *expr) {
    builder.push_back(' ');
    builder += print(expr);
  }

  void
  appendPart(std::string &builder,
             const std::vector<std::unique_ptr<eloxir::Expr>> &expressions) {
    for (const auto &expr : expressions) {
      appendPart(builder, expr.get());
    }
  }

  void appendPart(std::string &builder, const eloxir::Token &token) {
    builder.push_back(' ');
    builder += token.getLexeme();
  }

  void appendPart(std::string &builder, const std::string &text) {
    builder.push_back(' ');
    builder += text;
  }
};

class GlobalSyncCollector : public eloxir::ExprVisitor,
                            public eloxir::StmtVisitor {
public:
  explicit GlobalSyncCollector(
      const std::unordered_map<const eloxir::Expr *, int> &resolvedLocals)
      : resolvedLocals(resolvedLocals) {}

  std::unordered_set<std::string>
  collect(const std::vector<std::unique_ptr<eloxir::Stmt>> &statements) {
    for (const auto &stmt : statements) {
      visit(stmt.get());
    }
    return globals;
  }

  void visitBinaryExpr(eloxir::Binary *expr) override {
    visit(expr->left.get());
    visit(expr->right.get());
  }

  void visitGroupingExpr(eloxir::Grouping *expr) override {
    visit(expr->expression.get());
  }

  void visitLiteralExpr(eloxir::Literal *) override {}

  void visitUnaryExpr(eloxir::Unary *expr) override {
    visit(expr->right.get());
  }

  void visitVariableExpr(eloxir::Variable *expr) override {
    if (functionDepth > 0 &&
        resolvedLocals.find(expr) == resolvedLocals.end()) {
      const auto &name = expr->name.getLexeme();
      if (name != "clock" && name != "readLine") {
        globals.insert(name);
      }
    }
  }

  void visitAssignExpr(eloxir::Assign *expr) override {
    visit(expr->value.get());
    if (functionDepth > 0 &&
        resolvedLocals.find(expr) == resolvedLocals.end()) {
      globals.insert(expr->name.getLexeme());
    }
  }

  void visitLogicalExpr(eloxir::Logical *expr) override {
    visit(expr->left.get());
    visit(expr->right.get());
  }

  void visitCallExpr(eloxir::Call *expr) override {
    visit(expr->callee.get());
    for (const auto &arg : expr->arguments) {
      visit(arg.get());
    }
  }

  void visitGetExpr(eloxir::Get *expr) override { visit(expr->object.get()); }

  void visitSetExpr(eloxir::Set *expr) override {
    visit(expr->object.get());
    visit(expr->value.get());
  }

  void visitThisExpr(eloxir::This *) override {}
  void visitSuperExpr(eloxir::Super *) override {}

  void visitExpressionStmt(eloxir::Expression *stmt) override {
    visit(stmt->expression.get());
  }

  void visitPrintStmt(eloxir::Print *stmt) override {
    visit(stmt->expression.get());
  }

  void visitVarStmt(eloxir::Var *stmt) override {
    visit(stmt->initializer.get());
  }

  void visitBlockStmt(eloxir::Block *stmt) override {
    for (const auto &inner : stmt->statements) {
      visit(inner.get());
    }
  }

  void visitIfStmt(eloxir::If *stmt) override {
    visit(stmt->condition.get());
    visit(stmt->thenBranch.get());
    visit(stmt->elseBranch.get());
  }

  void visitWhileStmt(eloxir::While *stmt) override {
    visit(stmt->condition.get());
    visit(stmt->body.get());
  }

  void visitFunctionStmt(eloxir::Function *stmt) override {
    ++functionDepth;
    if (stmt->body) {
      visitBlockStmt(stmt->body.get());
    }
    --functionDepth;
  }

  void visitReturnStmt(eloxir::Return *stmt) override {
    visit(stmt->value.get());
  }

  void visitClassStmt(eloxir::Class *stmt) override {
    visit(stmt->superclass.get());
    for (const auto &method : stmt->methods) {
      visitFunctionStmt(method.get());
    }
  }

private:
  const std::unordered_map<const eloxir::Expr *, int> &resolvedLocals;
  std::unordered_set<std::string> globals;
  int functionDepth = 0;

  void visit(eloxir::Expr *expr) {
    if (expr)
      expr->accept(this);
  }

  void visit(eloxir::Stmt *stmt) {
    if (stmt)
      stmt->accept(this);
  }
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

std::string formatNumber(double value) {
  std::ostringstream out;
  out.precision(15);
  out << value;
  auto text = out.str();
  if (text.find('e') == std::string::npos &&
      text.find('E') == std::string::npos &&
      text.find('.') == std::string::npos) {
    text += ".0";
  }
  return text;
}

struct LiteralFormatter {
  std::string operator()(std::monostate) const { return "null"; }

  std::string operator()(double value) const { return formatNumber(value); }

  std::string operator()(const std::string &value) const { return value; }

  std::string operator()(bool value) const { return value ? "true" : "false"; }
};

std::string literalToString(const eloxir::Token &token) {
  return std::visit(LiteralFormatter{}, token.getLiteral());
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
      const std::string &lexeme = token.getLexeme();
      const std::string literal = literalToString(token);

      std::cout << tokenTypeName(token.getType());
      if (!lexeme.empty()) {
        std::cout << ' ' << lexeme;
      }
      if (!literal.empty()) {
        std::cout << ' ' << literal;
      }
      std::cout << '\n';
    }
    return static_cast<int>(ExitCode::kOk);
  } catch (const std::runtime_error &e) {
    std::cerr << "Scan error: " << e.what() << '\n';
    return static_cast<int>(ExitCode::kCompileError);
  }
}

int printAstFile(const std::string &filename) {
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
    eloxir::Parser parser(tokens);
    auto expr = parser.parseSingleExpression();
    if (!expr || parser.hadErrors()) {
      std::string message = parser.hadErrors() ? parser.firstErrorMessage()
                                               : "Failed to parse expression.";
      std::cerr << "Parse error: " << message << '\n';
      return static_cast<int>(ExitCode::kCompileError);
    }

    AstPrinter printer;
    std::cout << printer.print(expr.get()) << '\n';
    return static_cast<int>(ExitCode::kOk);
  } catch (const std::runtime_error &e) {
    std::cerr << "Parse error: " << e.what() << '\n';
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
  elx_set_object_tracking_enabled(0);

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
  auto runtimeSyncedGlobals =
      GlobalSyncCollector(resolver.locals).collect(stmts);
  auto codeGenPlan = eloxir::CodeGenPlan::analyze(stmts);

  const bool useLoxIRBackend =
      eloxir::loxir::loxirEnvFlag("ELOXIR_USE_LOXIR_BACKEND");
  const bool wantLoxIR =
      useLoxIRBackend || eloxir::loxir::loxirEnvFlag("ELOXIR_PRINT_LOXIR") ||
      std::getenv("ELOXIR_DUMP_LOXIR") != nullptr;
  const bool traceLoxIR =
      eloxir::loxir::loxirEnvFlag("ELOXIR_TRACE_PASSES") ||
      eloxir::loxir::loxirEnvFlag("ELOXIR_PRINT_LOXIR") ||
      std::getenv("ELOXIR_DUMP_LOXIR") != nullptr;
  bool loxIRVerified = true;
  std::unique_ptr<eloxir::loxir::LoxModule> loxModule;
  if (wantLoxIR) {
    eloxir::loxir::AstLowerer lowerer(&resolver.locals,
                                       &resolver.function_upvalues);
    loxModule = std::make_unique<eloxir::loxir::LoxModule>(
        lowerer.lower("file_module", stmts));
    if (hasOversizedLoxIRBlock(*loxModule)) {
      loxIRVerified = false;
      if (traceLoxIR) {
        std::cerr << "eloxir-loxir: falling back to legacy LLVM backend: "
                     "oversized block needs legacy diagnostics\n";
      }
    }
    auto verification = eloxir::loxir::verifyModule(*loxModule);
    if (!verification.ok) {
      loxIRVerified = false;
      if (traceLoxIR) {
        std::cerr << "LoxIR verification warnings:\n";
        for (const auto &message : verification.errors) {
          std::cerr << "  " << message << '\n';
        }
      }
    }
    eloxir::loxir::dumpModuleIfRequested(*loxModule, "lowered");

    auto passManager = eloxir::loxir::createDefaultLoxPassPipeline();
    passManager.run(*loxModule);
    auto optimizedVerification = eloxir::loxir::verifyModule(*loxModule);
    if (!optimizedVerification.ok) {
      loxIRVerified = false;
      if (traceLoxIR) {
        std::cerr << "Optimized LoxIR verification warnings:\n";
        for (const auto &message : optimizedVerification.errors) {
          std::cerr << "  " << message << '\n';
        }
      }
    }
    eloxir::loxir::dumpModuleIfRequested(*loxModule, "optimized");
  }

  // Clear any previous runtime errors
  elx_clear_runtime_error();

  try {
    if (useLoxIRBackend && loxModule && loxIRVerified) {
      auto jit = cantFail(eloxir::EloxirJIT::Create());
      auto fileCtx = std::make_unique<LLVMContext>();
      auto fileMod = std::make_unique<Module>("file_module", *fileCtx);
      fileMod->setDataLayout(jit->getDataLayout());
      fileMod->setTargetTriple(jit->getTargetTriple().str());

      auto unsupported =
          eloxir::loxir::emitLoxIRModuleToLLVM(*loxModule, *fileMod);
      if (!unsupported) {
        if (llvm::verifyModule(*fileMod, &llvm::errs())) {
          std::cerr << "Generated invalid LoxIR LLVM module. Cannot execute.\n";
          return static_cast<int>(ExitCode::kCompileError);
        }

        cantFail(jit->addModule(
            makeThreadSafeModule(std::move(fileMod), std::move(fileCtx))));
        auto sym = cantFail(jit->lookup("main"));
        using FnTy = uint64_t (*)();
        reinterpret_cast<FnTy>(sym.getAddress())();

        if (elx_has_runtime_error()) {
          elx_clear_runtime_error();
          return static_cast<int>(ExitCode::kRuntimeError);
        }
        return static_cast<int>(ExitCode::kOk);
      }

      if (eloxir::loxir::loxirEnvFlag("ELOXIR_TRACE_PASSES")) {
        std::cerr << "eloxir-loxir: falling back to legacy LLVM backend: "
                  << *unsupported << '\n';
      }
      elx_clear_runtime_error();
    } else if (useLoxIRBackend && loxModule && !loxIRVerified &&
               traceLoxIR) {
      std::cerr << "eloxir-loxir: falling back to legacy LLVM backend: "
                   "LoxIR verification failed\n";
    }

    auto jit = cantFail(eloxir::EloxirJIT::Create());

    // Create context and module for the entire file
    auto fileCtx = std::make_unique<LLVMContext>();
    auto fileMod = std::make_unique<Module>("file_module", *fileCtx);
    fileMod->setDataLayout(jit->getDataLayout());
    fileMod->setTargetTriple(jit->getTargetTriple().str());
    eloxir::CodeGenVisitor fileCG(*fileMod);

    // Pass resolver upvalue information to code generator
    fileCG.setResolverUpvalues(&resolver.function_upvalues);
    fileCG.setResolverLocals(&resolver.locals);
    fileCG.setRuntimeSyncedGlobals(std::move(runtimeSyncedGlobals));
    fileCG.setCodeGenPlan(&codeGenPlan);

    // Create main function
    auto fnTy = FunctionType::get(fileCG.llvmValueTy(), {}, false);
    auto fn =
        Function::Create(fnTy, Function::ExternalLinkage, "main", *fileMod);
    fileCG.getBuilder().SetInsertPoint(
        BasicBlock::Create(*fileCtx, "entry", fn));

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

    cantFail(jit->addModule(
        makeThreadSafeModule(std::move(fileMod), std::move(fileCtx))));

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

int runFileWithCacheStats(const std::string &filename) {
  if (!elx_cache_stats_enabled()) {
    int result = runFile(filename);
    elx_cache_stats_dump();
    return result;
  }

  elx_cache_stats_reset();
  int result = runFile(filename);
  elx_cache_stats_dump();
  return result;
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
      auto lineCtx = std::make_unique<LLVMContext>();
      auto lineMod = std::make_unique<Module>("repl_line", *lineCtx);
      lineMod->setDataLayout(jit->getDataLayout());
      lineMod->setTargetTriple(jit->getTargetTriple().str());
      eloxir::CodeGenVisitor lineCG(*lineMod);

      // Pass resolver upvalue information to code generator
      lineCG.setResolverUpvalues(&resolver.function_upvalues);
      lineCG.setResolverLocals(&resolver.locals);

      // wrap in unique function name to avoid duplicates
      std::string fnName = "__expr" + std::to_string(lineCount++);
      auto fnTy = FunctionType::get(lineCG.llvmValueTy(), {}, false);
      auto fn =
          Function::Create(fnTy, Function::ExternalLinkage, fnName, *lineMod);
      lineCG.getBuilder().SetInsertPoint(
          BasicBlock::Create(*lineCtx, "entry", fn));

      // Generate code for the statement/expression
      exprAST->codegen(lineCG);
      lineCG.getBuilder().CreateRet(lineCG.value);

      // Verify the function before executing
      if (llvm::verifyFunction(*fn, &llvm::errs())) {
        std::cerr << "Generated invalid LLVM IR. Skipping execution.\n";
        continue;
      }

      cantFail(jit->addModule(
          makeThreadSafeModule(std::move(lineMod), std::move(lineCtx))));

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

  if (option == "--print-ast") {
    if (argc != 3) {
      std::cerr << "Usage: " << argv[0] << " --print-ast <filename>\n";
      return static_cast<int>(ExitCode::kRuntimeError);
    }
    return printAstFile(argv[2]);
  }

  if (option == "--cache-stats") {
    if (argc != 3) {
      std::cerr << "Usage: " << argv[0] << " --cache-stats <filename>\n";
      return static_cast<int>(ExitCode::kRuntimeError);
    }
    return runFileWithCacheStats(argv[2]);
  }

  if (argc == 2) {
    return runFile(argv[1]);
  }

  std::cerr << "Usage: " << argv[0]
            << " [--scan <filename>] [--print-ast <filename>]"
            << " [--cache-stats <filename>] [filename]\n";
  std::cerr << "  No arguments: Start REPL\n";
  std::cerr << "  --scan <file>: Print tokens produced by scanner\n";
  std::cerr << "  --print-ast <file>: Print canonical AST for expression\n";
  std::cerr
      << "  --cache-stats <file>: Execute file and dump cache statistics\n";
  std::cerr << "  <file>: Execute file\n";
  return static_cast<int>(ExitCode::kRuntimeError);
}
