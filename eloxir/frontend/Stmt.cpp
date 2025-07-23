#include "Stmt.h"
#include "Visitor.h"

namespace eloxir {

// Forward declaration to avoid including CodeGenVisitor
class CodeGenVisitor;

// Stub implementations for codegen methods
void Expression::codegen(CodeGenVisitor &cg) {
  // TODO: Implement when codegen is ready
}

void Print::codegen(CodeGenVisitor &cg) {
  // TODO: Implement when codegen is ready
}

void Var::codegen(CodeGenVisitor &cg) {
  // TODO: Implement when codegen is ready
}

void Block::codegen(CodeGenVisitor &cg) {
  // TODO: Implement when codegen is ready
}

void If::codegen(CodeGenVisitor &cg) {
  // TODO: Implement when codegen is ready
}

void While::codegen(CodeGenVisitor &cg) {
  // TODO: Implement when codegen is ready
}

void Function::codegen(CodeGenVisitor &cg) {
  // TODO: Implement when codegen is ready
}

void Return::codegen(CodeGenVisitor &cg) {
  // TODO: Implement when codegen is ready
}

void Class::codegen(CodeGenVisitor &cg) {
  // TODO: Implement when codegen is ready
}

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
