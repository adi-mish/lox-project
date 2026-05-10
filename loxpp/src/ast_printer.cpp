#include "ast_printer.h"

#include "token.h"

namespace loxpp {

std::string AstPrinter::print(const ExprPtr& expr) {
  if (!expr) {
    return "";
  }
  return expr->accept(*this);
}

std::string AstPrinter::print(const StmtPtr& stmt) {
  if (!stmt) {
    return "";
  }
  return stmt->accept(*this);
}

std::string AstPrinter::visitAssignExpr(AssignExpr& expr) {
  return parenthesize("=", {expr.name.lexeme, print(expr.value)});
}

std::string AstPrinter::visitBinaryExpr(BinaryExpr& expr) {
  return parenthesize(expr.op.lexeme, {print(expr.left), print(expr.right)});
}

std::string AstPrinter::visitCallExpr(CallExpr& expr) {
  std::vector<std::string> parts;
  parts.push_back(print(expr.callee));
  for (const auto& argument : expr.arguments) {
    parts.push_back(print(argument));
  }
  return parenthesize("call", parts);
}

std::string AstPrinter::visitGetExpr(GetExpr& expr) {
  return parenthesize(".", {print(expr.object), expr.name.lexeme});
}

std::string AstPrinter::visitGroupingExpr(GroupingExpr& expr) {
  return parenthesize("group", {print(expr.expression)});
}

std::string AstPrinter::visitLiteralExpr(LiteralExpr& expr) {
  return astLiteralToString(expr.value);
}

std::string AstPrinter::visitLogicalExpr(LogicalExpr& expr) {
  return parenthesize(expr.op.lexeme, {print(expr.left), print(expr.right)});
}

std::string AstPrinter::visitSetExpr(SetExpr& expr) {
  return parenthesize("=",
                      {print(expr.object), expr.name.lexeme, print(expr.value)});
}

std::string AstPrinter::visitSuperExpr(SuperExpr& expr) {
  return parenthesize("super", {expr.method.lexeme});
}

std::string AstPrinter::visitThisExpr(ThisExpr&) {
  return "this";
}

std::string AstPrinter::visitUnaryExpr(UnaryExpr& expr) {
  return parenthesize(expr.op.lexeme, {print(expr.right)});
}

std::string AstPrinter::visitVariableExpr(VariableExpr& expr) {
  return expr.name.lexeme;
}

std::string AstPrinter::visitBlockStmt(BlockStmt& stmt) {
  std::string result = "(block ";
  for (const auto& statement : stmt.statements) {
    result += print(statement);
  }
  result += ")";
  return result;
}

std::string AstPrinter::visitClassStmt(ClassStmt& stmt) {
  std::string result = "(class " + stmt.name.lexeme;
  if (stmt.superclass) {
    result += " < " + print(stmt.superclass);
  }
  for (const auto& method : stmt.methods) {
    result += " " + print(method);
  }
  result += ")";
  return result;
}

std::string AstPrinter::visitExpressionStmt(ExpressionStmt& stmt) {
  return parenthesize(";", {print(stmt.expression)});
}

std::string AstPrinter::visitFunctionStmt(FunctionStmt& stmt) {
  std::string result = "(fun " + stmt.name.lexeme + "(";
  for (std::size_t i = 0; i < stmt.params.size(); ++i) {
    if (i != 0) {
      result += " ";
    }
    result += stmt.params[i].lexeme;
  }
  result += ") ";
  for (const auto& body_stmt : stmt.body) {
    result += print(body_stmt);
  }
  result += ")";
  return result;
}

std::string AstPrinter::visitIfStmt(IfStmt& stmt) {
  if (!stmt.else_branch) {
    return parenthesize("if", {print(stmt.condition), print(stmt.then_branch)});
  }
  return parenthesize("if-else",
                      {print(stmt.condition), print(stmt.then_branch),
                       print(stmt.else_branch)});
}

std::string AstPrinter::visitPrintStmt(PrintStmt& stmt) {
  return parenthesize("print", {print(stmt.expression)});
}

std::string AstPrinter::visitReturnStmt(ReturnStmt& stmt) {
  if (!stmt.value) {
    return "(return)";
  }
  return parenthesize("return", {print(stmt.value)});
}

std::string AstPrinter::visitVarStmt(VarStmt& stmt) {
  if (!stmt.initializer) {
    return parenthesize("var", {stmt.name.lexeme});
  }
  return parenthesize("var", {stmt.name.lexeme, "=", print(stmt.initializer)});
}

std::string AstPrinter::visitWhileStmt(WhileStmt& stmt) {
  return parenthesize("while", {print(stmt.condition), print(stmt.body)});
}

std::string AstPrinter::parenthesize(std::string_view name,
                                     const std::vector<std::string>& parts) {
  std::string result = "(";
  result += name;
  for (const auto& part : parts) {
    result += " ";
    result += part;
  }
  result += ")";
  return result;
}

}  // namespace loxpp
