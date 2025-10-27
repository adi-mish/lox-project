# Eloxir test status (temporary notes)

## Outstanding suites
- Resolver/codegen still lets invalid programs fall through (e.g. too many locals/constants) instead of halting with compile errors.
- Method binding/`this` semantics are broken; closures lose the bound instance and emit "Can only call functions and closures."

## Recent investigations

### assignment/undefined.lox
- Failure mode: LLVM verifier rejects the module because `emitRuntimeError()` boxed the diagnostic string via `stringConst()` (returns a tagged `Value`) before calling `elx_runtime_error(const char*)`.
- Fix: pass the raw `i8*` from `CreateGlobalStringPtr` directly to `elx_runtime_error` so the runtime sees a C string and the verifier accepts the call.
- Status: resolved.

### scanning/*
- Failure mode: CLI executed scanner fixtures as full programs, so parser raised syntax errors.
- Fix: add `--scan` mode to the `eloxir` binary and teach the Python harness to invoke it for `test/scanning/**` files, printing tokens in Crafting Interpreters format.
- Status: resolved.

### variable/collide_with_parameter.lox
- Failure mode: function parameters lived in a parent resolver scope, so `var a;` inside the body silently shadowed the parameter instead of raising a compile error.
- Fix: resolve function bodies directly within the parameter scope rather than delegating to the block visitor that introduces another scope.
- Status: resolved.

### while/*
- Failure mode: runtime error calls for undefined variables/closures passed boxed `Value` handles to `elx_runtime_error`, confusing LLVM's verifier.
- Fix: emit plain `i8*` constants for diagnostics so the verifier accepts the module; while-suite programs now execute and return expected results.
- Status: resolved.

### nested_closure_capture.lox (resolved earlier)
- Shared `variableStacks` allowed closures to capture outer allocas directly; clearing stacks per function restored proper upvalue lifting.

### for_no_increment.lox (resolved earlier)
- Rewrote `for` desugaring to keep increment clause even when missing in source, preventing infinite loops.

### stack_overflow.lox (resolved earlier)
- Added runtime call-depth guard so deep recursion raises "Stack overflow." instead of segfaulting.
