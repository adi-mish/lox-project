#include "expr.h"

namespace loxpp {

#define LOXPP_EXPR_ACCEPTS(TypeName, VisitName)              \
  Value TypeName::accept(ExprValueVisitor& visitor) {        \
    return visitor.VisitName(*this);                         \
  }                                                          \
  void TypeName::accept(ExprVoidVisitor& visitor) {          \
    visitor.VisitName(*this);                                \
  }                                                          \
  std::string TypeName::accept(ExprStringVisitor& visitor) { \
    return visitor.VisitName(*this);                         \
  }

LOXPP_EXPR_ACCEPTS(AssignExpr, visitAssignExpr)
LOXPP_EXPR_ACCEPTS(BinaryExpr, visitBinaryExpr)
LOXPP_EXPR_ACCEPTS(CallExpr, visitCallExpr)
LOXPP_EXPR_ACCEPTS(GetExpr, visitGetExpr)
LOXPP_EXPR_ACCEPTS(GroupingExpr, visitGroupingExpr)
LOXPP_EXPR_ACCEPTS(LiteralExpr, visitLiteralExpr)
LOXPP_EXPR_ACCEPTS(LogicalExpr, visitLogicalExpr)
LOXPP_EXPR_ACCEPTS(SetExpr, visitSetExpr)
LOXPP_EXPR_ACCEPTS(SuperExpr, visitSuperExpr)
LOXPP_EXPR_ACCEPTS(ThisExpr, visitThisExpr)
LOXPP_EXPR_ACCEPTS(UnaryExpr, visitUnaryExpr)
LOXPP_EXPR_ACCEPTS(VariableExpr, visitVariableExpr)

#undef LOXPP_EXPR_ACCEPTS

}  // namespace loxpp
