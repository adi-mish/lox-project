#include "error_reporter.h"

#include <iostream>

namespace loxpp {

void ErrorReporter::error(int line, std::string_view message) {
  report(line, "", message);
}

void ErrorReporter::error(const Token& token, std::string_view message) {
  if (token.type == TokenType::Eof) {
    report(token.line, " at end", message);
  } else {
    report(token.line, " at '" + token.lexeme + "'", message);
  }
}

void ErrorReporter::report(int line,
                           std::string_view where,
                           std::string_view message) {
  std::cerr << "[line " << line << "] Error" << where << ": " << message
            << '\n';
  had_error_ = true;
}

}  // namespace loxpp
