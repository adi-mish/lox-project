#include "stmt.h"

namespace loxpp {

#define LOXPP_STMT_ACCEPTS(TypeName, VisitName)              \
  void TypeName::accept(StmtVoidVisitor& visitor) {          \
    visitor.VisitName(*this);                                \
  }                                                          \
  std::string TypeName::accept(StmtStringVisitor& visitor) { \
    return visitor.VisitName(*this);                         \
  }

LOXPP_STMT_ACCEPTS(BlockStmt, visitBlockStmt)
LOXPP_STMT_ACCEPTS(ClassStmt, visitClassStmt)
LOXPP_STMT_ACCEPTS(ExpressionStmt, visitExpressionStmt)
LOXPP_STMT_ACCEPTS(FunctionStmt, visitFunctionStmt)
LOXPP_STMT_ACCEPTS(IfStmt, visitIfStmt)
LOXPP_STMT_ACCEPTS(PrintStmt, visitPrintStmt)
LOXPP_STMT_ACCEPTS(ReturnStmt, visitReturnStmt)
LOXPP_STMT_ACCEPTS(VarStmt, visitVarStmt)
LOXPP_STMT_ACCEPTS(WhileStmt, visitWhileStmt)

#undef LOXPP_STMT_ACCEPTS

}  // namespace loxpp
