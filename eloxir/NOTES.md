# Eloxir test status (temporary notes)

## Failing cases after initial test run
- `closure/nested_closure.lox`: LLVM verifier errors due to cross-function instruction references when loading captured locals (scope1_decl etc.).
- `limit/stack_overflow.lox`: aborts with exit code -11 (likely stack overflow from runaway recursion or infinite loop?). Need to investigate.
- `limit/too_many_upvalues.lox`: similar LLVM verifier errors referencing instructions in other functions for captured variables.

## Timeouts with 3s budget (not necessarily bugs)
- `benchmark/equality.lox` (~4.5s runtime, completes successfully)
- `benchmark/instantiation.lox`
- `benchmark/invocation.lox`
- `benchmark/properties.lox`
- `benchmark/string_equality.lox`
- `benchmark/zoo_batch.lox`
- `for/syntax.lox`: infinite loop printing `1`. Real bug (should report parser errors?).


### nested_closure_capture.lox
- Reproduction confirms LLVM verifier error due to cross-function alloca usage via shared `variableStacks` across function boundaries.
- Root cause: when entering a new function, `variableStacks` retains pointers from enclosing scopes, so inner closures load from outer allocas directly instead of capturing values.

### for_no_increment.lox
- Reproduction loops forever printing 1.
- Root cause: Parser's special-case transformation for `for` loops with `var` initializer rewrites loop to use shadow `*_outer` variable but skips increment when original increment clause missing, leaving outer variable constant so condition never changes.

### stack_overflow.lox
- Segfault instead of runtime error.
- Root cause: Runtime lacks call depth guard; unbounded recursion eventually crashes host stack. Need global counter to emit "Stack overflow." before C++ stack blows.

## Status updates
- nested_closure_capture.lox now prints `ok` after isolating `variableStacks` per function.
- for_no_increment.lox now terminates with 1,2,3 after restoring canonical `for` desugaring.
- stack_overflow.lox now reports runtime error via call-depth guard.
- Full official suite passes; benchmark cases require tens of seconds each (e.g. instantiation ~59s) but complete successfully.
