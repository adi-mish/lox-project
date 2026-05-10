#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include "scanner.h"
#include "vm.h"

using namespace cpplox;

namespace {

std::string readFile(std::string_view path) {
  std::ifstream file(std::string(path), std::ios::binary);
  if (!file) {
    throw std::runtime_error("Could not open file \"" + std::string(path) +
                             "\".");
  }

  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

int runSource(Vm &vm, const std::string &source) {
  InterpretResult result = vm.interpret(source);
  if (result == INTERPRET_COMPILE_ERROR)
    return 65;
  if (result == INTERPRET_RUNTIME_ERROR)
    return 70;
  return 0;
}

int runFile(Vm &vm, std::string_view path) {
  try {
    return runSource(vm, readFile(path));
  } catch (const std::runtime_error &error) {
    std::cerr << error.what() << '\n';
    return 74;
  }
}

void repl(Vm &vm) {
  std::string line;
  for (;;) {
    std::cout << "> ";
    if (!std::getline(std::cin, line)) {
      std::cout << '\n';
      break;
    }
    vm.interpret(line);
  }
}

std::string_view tokenTypeName(TokenType type) {
  switch (type) {
  case TOKEN_LEFT_PAREN:
    return "LEFT_PAREN";
  case TOKEN_RIGHT_PAREN:
    return "RIGHT_PAREN";
  case TOKEN_LEFT_BRACE:
    return "LEFT_BRACE";
  case TOKEN_RIGHT_BRACE:
    return "RIGHT_BRACE";
  case TOKEN_COMMA:
    return "COMMA";
  case TOKEN_DOT:
    return "DOT";
  case TOKEN_MINUS:
    return "MINUS";
  case TOKEN_PLUS:
    return "PLUS";
  case TOKEN_SEMICOLON:
    return "SEMICOLON";
  case TOKEN_SLASH:
    return "SLASH";
  case TOKEN_STAR:
    return "STAR";
  case TOKEN_BANG:
    return "BANG";
  case TOKEN_BANG_EQUAL:
    return "BANG_EQUAL";
  case TOKEN_EQUAL:
    return "EQUAL";
  case TOKEN_EQUAL_EQUAL:
    return "EQUAL_EQUAL";
  case TOKEN_GREATER:
    return "GREATER";
  case TOKEN_GREATER_EQUAL:
    return "GREATER_EQUAL";
  case TOKEN_LESS:
    return "LESS";
  case TOKEN_LESS_EQUAL:
    return "LESS_EQUAL";
  case TOKEN_IDENTIFIER:
    return "IDENTIFIER";
  case TOKEN_STRING:
    return "STRING";
  case TOKEN_NUMBER:
    return "NUMBER";
  case TOKEN_AND:
    return "AND";
  case TOKEN_CLASS:
    return "CLASS";
  case TOKEN_ELSE:
    return "ELSE";
  case TOKEN_FALSE:
    return "FALSE";
  case TOKEN_FOR:
    return "FOR";
  case TOKEN_FUN:
    return "FUN";
  case TOKEN_IF:
    return "IF";
  case TOKEN_NIL:
    return "NIL";
  case TOKEN_OR:
    return "OR";
  case TOKEN_PRINT:
    return "PRINT";
  case TOKEN_RETURN:
    return "RETURN";
  case TOKEN_SUPER:
    return "SUPER";
  case TOKEN_THIS:
    return "THIS";
  case TOKEN_TRUE:
    return "TRUE";
  case TOKEN_VAR:
    return "VAR";
  case TOKEN_WHILE:
    return "WHILE";
  case TOKEN_ERROR:
    return "ERROR";
  case TOKEN_EOF:
    return "EOF";
  }
  return "UNKNOWN";
}

std::string_view lexeme(Token token) {
  return {token.start, static_cast<std::size_t>(token.length)};
}

void writeLexeme(Token token) { std::cout.write(token.start, token.length); }

void printScanToken(Token token) {
  if (token.type == TOKEN_EOF) {
    std::cout << "EOF null\n";
    return;
  }

  std::cout << tokenTypeName(token.type) << ' ';
  writeLexeme(token);

  switch (token.type) {
  case TOKEN_NUMBER:
    std::cout << ' ';
    writeLexeme(token);
    if (lexeme(token).find('.') == std::string_view::npos) {
      std::cout << ".0";
    }
    std::cout << '\n';
    break;
  case TOKEN_STRING:
    if (token.length > 2) {
      std::cout << ' ';
      std::cout.write(token.start + 1, token.length - 2);
    }
    std::cout << '\n';
    break;
  default:
    std::cout << " null\n";
    break;
  }
}

int scanSource(const std::string &source) {
  Scanner scanner(source);
  bool hadError = false;

  for (;;) {
    Token token = scanner.scanToken();
    if (token.type == TOKEN_ERROR) {
      std::cerr << "[line " << token.line << "] Error: ";
      std::cerr.write(token.start, token.length);
      std::cerr << '\n';
      hadError = true;
      continue;
    }

    printScanToken(token);
    if (token.type == TOKEN_EOF)
      break;
  }

  return hadError ? 65 : 0;
}

int scanFile(std::string_view path) {
  try {
    return scanSource(readFile(path));
  } catch (const std::runtime_error &error) {
    std::cerr << error.what() << '\n';
    return 74;
  }
}

} // namespace

int main(int argc, const char *argv[]) {
  Vm vm;
  bool scan = false;
  bool stats = false;
  const char *path = nullptr;

  for (int i = 1; i < argc; i++) {
    std::string_view arg(argv[i]);
    if (arg == "--scan") {
      scan = true;
    } else if (arg == "--stats") {
      stats = true;
    } else if (path == nullptr) {
      path = argv[i];
    } else {
      std::cerr << "Usage: cpplox [--stats] [--scan] [path]\n";
      return 64;
    }
  }

#ifdef CPPLOX_ENABLE_VM_STATS
  vm.setStatsEnabled(stats);
  vm.resetStats();
#else
  if (stats) {
    std::cerr << "cpplox was built without CPPLOX_ENABLE_VM_STATS.\n";
    return 64;
  }
#endif

  int exitCode = 0;
  if (scan) {
    if (path == nullptr) {
      std::cerr << "Usage: cpplox [--stats] --scan [path]\n";
      return 64;
    }
    exitCode = scanFile(path);
  } else if (path == nullptr) {
    repl(vm);
  } else {
    exitCode = runFile(vm, path);
  }

#ifdef CPPLOX_ENABLE_VM_STATS
  if (stats) {
    vm.printStats();
  }
#endif
  return exitCode;
}
