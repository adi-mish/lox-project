#include "Stmt.h"
#include "Visitor.h"

namespace eloxir {

// Accept method implementations
void Expression::accept(StmtVisitor *v) { v->visitExpressionStmt(this); }
void Print::accept(StmtVisitor *v) { v->visitPrintStmt(this); }
void Var::accept(StmtVisitor *v) { v->visitVarStmt(this); }
void Block::accept(StmtVisitor *v) { v->visitBlockStmt(this); }
void If::accept(StmtVisitor *v) { v->visitIfStmt(this); }
void While::accept(StmtVisitor *v) { v->visitWhileStmt(this); }
void Function::accept(StmtVisitor *v) { v->visitFunctionStmt(this); }
void Return::accept(StmtVisitor *v) { v->visitReturnStmt(this); }
void Class::accept(StmtVisitor *v) { v->visitClassStmt(this); }

} // namespace eloxir
