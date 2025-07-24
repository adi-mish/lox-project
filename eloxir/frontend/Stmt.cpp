#include "Stmt.h"
#include "../codegen/CodeGenVisitor.h"
#include "Visitor.h"

namespace eloxir {

// Implementations for codegen methods - delegate to visitor
void Expression::codegen(CodeGenVisitor &cg) { cg.visitExpressionStmt(this); }

void Print::codegen(CodeGenVisitor &cg) { cg.visitPrintStmt(this); }

void Var::codegen(CodeGenVisitor &cg) { cg.visitVarStmt(this); }

void Block::codegen(CodeGenVisitor &cg) { cg.visitBlockStmt(this); }

void If::codegen(CodeGenVisitor &cg) { cg.visitIfStmt(this); }

void While::codegen(CodeGenVisitor &cg) { cg.visitWhileStmt(this); }

void Function::codegen(CodeGenVisitor &cg) { cg.visitFunctionStmt(this); }

void Return::codegen(CodeGenVisitor &cg) { cg.visitReturnStmt(this); }

void Class::codegen(CodeGenVisitor &cg) { cg.visitClassStmt(this); }

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
