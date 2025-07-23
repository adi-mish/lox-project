#include "Expr.h"
#include "Visitor.h"

namespace eloxir {

// Accept method implementations
void Binary::accept(ExprVisitor *v) { v->visitBinaryExpr(this); }
void Grouping::accept(ExprVisitor *v) { v->visitGroupingExpr(this); }
void Literal::accept(ExprVisitor *v) { v->visitLiteralExpr(this); }
void Unary::accept(ExprVisitor *v) { v->visitUnaryExpr(this); }
void Variable::accept(ExprVisitor *v) { v->visitVariableExpr(this); }
void Assign::accept(ExprVisitor *v) { v->visitAssignExpr(this); }
void Logical::accept(ExprVisitor *v) { v->visitLogicalExpr(this); }
void Call::accept(ExprVisitor *v) { v->visitCallExpr(this); }
void Get::accept(ExprVisitor *v) { v->visitGetExpr(this); }
void Set::accept(ExprVisitor *v) { v->visitSetExpr(this); }
void This::accept(ExprVisitor *v) { v->visitThisExpr(this); }
void Super::accept(ExprVisitor *v) { v->visitSuperExpr(this); }

} // namespace eloxir
