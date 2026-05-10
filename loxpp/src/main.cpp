#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "ast_printer.h"
#include "error_reporter.h"
#include "parser.h"
#include "scanner.h"

namespace {

enum class Mode {
  Run,
  Scan,
  PrintAst,
};

std::string readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open file '" + path + "'.");
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string formatToken(const loxpp::Token& token) {
  if (token.type == loxpp::TokenType::Eof) {
    return "EOF null";
  }

  if (std::holds_alternative<std::monostate>(token.literal)) {
    return std::string(loxpp::tokenTypeName(token.type)) + " " +
           token.lexeme + " null";
  }

  if (const auto* text = std::get_if<std::string>(&token.literal);
      text != nullptr && text->empty()) {
    return std::string(loxpp::tokenTypeName(token.type)) + " " +
           token.lexeme;
  }

  return std::string(loxpp::tokenTypeName(token.type)) + " " + token.lexeme +
         " " + loxpp::literalToString(token.literal);
}

class LoxApp {
 public:
  void runFile(const std::string& path, Mode mode) {
    run(readFile(path), mode);
    if (reporter_.hadError()) {
      std::exit(65);
    }
  }

  void runPrompt() {
    std::string line;
    while (true) {
      std::cout << "> ";
      if (!std::getline(std::cin, line)) {
        break;
      }

      run(line, Mode::Run);
      reporter_.resetError();
    }
  }

 private:
  void run(const std::string& source, Mode mode) {
    loxpp::Scanner scanner(source, reporter_);
    const auto tokens = scanner.scanTokens();

    if (mode == Mode::Scan) {
      for (const auto& token : tokens) {
        std::cout << formatToken(token) << '\n';
      }
      return;
    }

    if (mode == Mode::PrintAst) {
      loxpp::Parser parser(tokens, reporter_);
      const auto expression = parser.parseExpression();
      if (reporter_.hadError()) {
        return;
      }
      std::cout << loxpp::AstPrinter().print(expression) << '\n';
      return;
    }

    reporter_.error(1, "Execution is not implemented yet.");
  }

  loxpp::ErrorReporter reporter_;
};

}  // namespace

int main(int argc, char* argv[]) {
  Mode mode = Mode::Run;
  std::string path;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--scan") {
      mode = Mode::Scan;
    } else if (arg == "--print-ast") {
      mode = Mode::PrintAst;
    } else {
      if (!path.empty()) {
        std::cout << "Usage: loxpp [--scan|--print-ast] [script]\n";
        return 64;
      }
      path = arg;
    }
  }

  try {
    LoxApp app;
    if (!path.empty()) {
      app.runFile(path, mode);
    } else if (mode == Mode::Run) {
      app.runPrompt();
    } else {
      std::cout << "Usage: loxpp [--scan|--print-ast] [script]\n";
      return 64;
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 74;
  }

  return 0;
}
