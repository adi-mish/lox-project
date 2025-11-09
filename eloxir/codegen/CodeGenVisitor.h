#pragma once
#include "../frontend/Expr.h"
#include "../frontend/Stmt.h"
#include "../frontend/Visitor.h"
#include "../runtime/Value.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <cstddef>
#include <cstdint>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eloxir {

class CodeGenVisitor : public ExprVisitor, public StmtVisitor {
  llvm::IRBuilder<> builder;
  llvm::LLVMContext &ctx;
  llvm::Module &mod;
  // current lexical scope:
  std::unordered_map<std::string, llvm::Value *> locals;
  // track which locals are direct values (like parameters) vs alloca'd
  std::unordered_set<std::string> directValues;
  // function table for user-defined functions
  std::unordered_map<std::string, llvm::Function *> functions;
  // current function being compiled (for return statements)
  llvm::Function *currentFunction;

  static constexpr int MAX_PARAMETERS = 255;
  static constexpr int MAX_CONSTANTS = 256;
  static constexpr int MAX_LOCAL_SLOTS = 256;
  static constexpr int MAX_USER_LOCAL_SLOTS = MAX_LOCAL_SLOTS - 1;
  static constexpr int MAX_UPVALUES = 256;
  static constexpr std::size_t MAX_LOOP_BODY_INSTRUCTIONS = 65535;

  // Track block nesting depth to distinguish true globals from block-scoped
  // variables
  int blockDepth = 0;

  // Per-loop instruction accounting to mirror Crafting Interpreters limits.
  std::vector<uint32_t> loopInstructionCounts;

  // Counter for creating unique variable names in loop contexts
  int variableCounter = 0;

  // Track block re-execution for proper loop variable scoping
  std::unordered_map<const Block *, int> blockExecutionCount;

  // Track variables that are declared in for-loop initializers
  std::unordered_set<std::string> loopVariables;

  // Per-variable storage stack for proper lexical scoping
  // Maps variable name to stack of storage locations (most recent = back())
  std::unordered_map<std::string, std::vector<llvm::Value *>> variableStacks;
  std::vector<llvm::Value *> global_local_slots;
  std::unordered_set<llvm::Value *> global_captured_slots;
  // Track the last allocated stack slot per function so new allocas preserve
  // lexical order. This keeps pointer ordering consistent with Crafting
  // Interpreters' stack model for upvalue closing semantics.
  std::unordered_map<llvm::Function *, llvm::Instruction *> lastAllocaForFunction;

  struct PropertyCacheEntry {
    llvm::GlobalVariable *shapeBits;
    llvm::GlobalVariable *slotIndex;
  };
  std::unordered_map<const Expr *, PropertyCacheEntry> propertyCaches;
  int propertyCacheCounter = 0;

  enum class MethodContext { NONE, METHOD, INITIALIZER };

  // Function context for closure support
  struct FunctionContext {
    llvm::Function *llvm_function;
    std::unordered_map<std::string, llvm::Value *> locals;
    std::unordered_set<std::string> direct_values;
    std::vector<std::string> upvalues; // Names of captured variables
    std::unordered_map<std::string, int> upvalue_indices;
    llvm::Value *upvalue_array = nullptr; // Array parameter for upvalues
    int constantCount = 0;
    int localCount = 0;
    int upvalueCount = 0;
    std::string debug_name;
    std::vector<llvm::Value *> local_slots;
    std::unordered_set<llvm::Value *> captured_slots;
    MethodContext method_context = MethodContext::NONE;
  };

  std::stack<FunctionContext> function_stack;

  int globalConstantCount = 0;

  // Deferred function objects to create in global context
  std::vector<std::pair<std::string, int>> pendingFunctions; // name, arity

  MethodContext method_context_override = MethodContext::NONE;
  llvm::Value *current_class_value = nullptr;
  std::string function_map_key_override;

public:
  CodeGenVisitor(llvm::Module &m);

  llvm::Type *llvmValueTy() const;
  llvm::Value *value; // last visited Expr result

  // Safe accessors for the builder / module
  llvm::IRBuilder<> &getBuilder() { return builder; }
  llvm::Module &getModule() { return mod; }
  llvm::LLVMContext &getContext() { return ctx; }

  // Access to globals for REPL persistence
  std::unordered_map<std::string, llvm::Value *> globals;

  // Track which variables are global (to distinguish from truly local
  // variables)
  std::unordered_set<std::string> globalVariables;

  // Helper for forward function declarations
  void declareFunctionSignature(Function *s);

  // Helper to create function object from LLVM function
  llvm::Value *createFunctionObject(const std::string &funcName,
                                    llvm::Function *llvmFunc, int arity);

  // Helper to create function object immediately (works in nested contexts)
  llvm::Value *createFunctionObjectImmediate(const std::string &funcName,
                                             llvm::Function *llvmFunc,
                                             int arity);

  // Closure support helpers
  bool isUpvalue(const std::string &name);
  llvm::Value *createClosureObject(llvm::Function *func,
                                   const std::vector<std::string> &upvalues);
  llvm::Value *createDeferredClosure(llvm::Function *func,
                                     const std::vector<std::string> &upvalues,
                                     int arity,
                                     const std::string &funcName = "");
  llvm::Value *createDeferredClosureWithCapturedUpvalues(
      llvm::Function *func, const std::vector<std::string> &upvalues,
      const std::unordered_map<std::string, llvm::Value *> &capturedUpvalues,
      int arity, const std::string &funcName = "");
  llvm::Value *accessUpvalue(const std::string &name, int index);
  llvm::Value *captureUpvalue(const std::string &name);

  // Access to resolver upvalue information
  const std::unordered_map<const Function *, std::vector<std::string>>
      *resolver_upvalues;
  const std::unordered_map<const Expr *, int> *resolver_locals;

public:
  void setResolverUpvalues(
      const std::unordered_map<const Function *, std::vector<std::string>>
          *upvalues) {
    resolver_upvalues = upvalues;
  }

  void setResolverLocals(const std::unordered_map<const Expr *, int> *locals) {
    resolver_locals = locals;
  }

  // Helper to create global function objects outside of function contexts
  void createGlobalFunctionObjects();

  // == Expr nodes ==================================================
  void visitBinaryExpr(Binary *e) override;
  void visitGroupingExpr(Grouping *e) override;
  void visitLiteralExpr(Literal *e) override;
  void visitUnaryExpr(Unary *e) override;
  void visitVariableExpr(Variable *e) override;
  void visitAssignExpr(Assign *e) override;
  void visitLogicalExpr(Logical *e) override;
  void visitCallExpr(Call *e) override;
  void visitGetExpr(Get *e) override;
  void visitSetExpr(Set *e) override;
  void visitThisExpr(This *e) override;
  void visitSuperExpr(Super *e) override;

  // == Stmt nodes ==================================================
  void visitExpressionStmt(Expression *s) override;
  void visitPrintStmt(Print *s) override;
  void visitVarStmt(Var *s) override;
  void
  visitVarStmtWithExecution(Var *s,
                            int blockExecution); // Helper for loop variables
  void visitBlockStmt(Block *s) override;
  void visitIfStmt(If *s) override;
  void visitWhileStmt(While *s) override;
  void visitFunctionStmt(Function *s) override;
  void visitReturnStmt(Return *s) override;
  void visitClassStmt(Class *s) override;

private:
  std::size_t estimateLoopBodyInstructions(Stmt *stmt) const;
  std::size_t saturatingLoopAdd(std::size_t current, std::size_t increment) const;
  llvm::Value *tagOf(llvm::Value *v);
  llvm::Value *isNumber(llvm::Value *v);
  llvm::Value *isString(llvm::Value *v);
  llvm::Value *toDouble(llvm::Value *v);
  llvm::Value *fromDouble(llvm::Value *d);
  llvm::Value *boolConst(bool b);
  llvm::Value *nilConst();
  llvm::Value *makeBool(llvm::Value *i1);
  llvm::Value *stringConst(const std::string &str,
                           bool countAsConstant = false);
  PropertyCacheEntry &ensurePropertyCache(const Expr *expr);
  llvm::Value *isFalsy(llvm::Value *v);  // returns i1
  llvm::Value *isTruthy(llvm::Value *v); // returns i1 (= !isFalsy)
  llvm::AllocaInst *createStackAlloca(llvm::Function *fn, llvm::Type *type,
                                      const std::string &name);

  // New helper methods for proper comparisons
  llvm::Value *valuesEqual(llvm::Value *L, llvm::Value *R); // returns i1
  llvm::Value *checkBothNumbers(llvm::Value *L, llvm::Value *R,
                                llvm::BasicBlock *&successBB,
                                llvm::BasicBlock *&errorBB); // returns i1

  // Error propagation helper
  void emitRuntimeError(const std::string &message);
  void checkRuntimeError(llvm::Value *returnValue = nullptr);

  void recordConstant();
  void ensureParameterLimit(size_t arity);
  void closeAllCapturedLocals();
  bool removeLocalSlot(llvm::Value *slot);

  void enterLoop();
  void exitLoop();
  void addLoopInstructions(std::size_t count);

  class LoopInstructionScopeReset {
  public:
    explicit LoopInstructionScopeReset(CodeGenVisitor &visitor);
    ~LoopInstructionScopeReset();

  private:
    CodeGenVisitor &visitor;
    std::size_t depth;
  };
};

} // namespace eloxir
