# Eloxir Status Report

## Build
- Configured with `cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo` under `eloxir/build`.
- Latest compile succeeded via `cmake --build eloxir/build --target eloxir`.

## Test Matrix
- `pytest eloxir/tests -q` — **pass** (15 regression cases for runtime and compiler edge conditions).
- `python eloxir/tools/run_official_tests.py --filter limit/stack_overflow.lox` — **pass** (recursion now surfaces a managed runtime error instead of segfaulting).
- `python eloxir/tools/run_official_tests.py --filter benchmark/*` — **pass** (11/11). The harness now grants benchmark cases up to 60 s each so the slowest fixtures (`trees.lox`, `binary_trees.lox`) can complete.

## Outstanding Issues

None at present.

## Additional Notes
- Instance layout now relies on `ObjShape` hidden classes and shape transitions instead of mutating per-class `DenseMap` slot tables. Instances share contiguous field storage that is resized when the shape grows, reducing hash lookups during property reads and writes.
- Property equality checks fast-path interned strings by comparing pointers, and the runtime guard (`elx_strings_equal_interned`) now resolves interned comparisons with pure pointer equality instead of deferring to a full `memcmp`.
- Property accessors (`obj.field` and `obj.field = value`) now lower to per-expression inline caches backed by new runtime helpers (`elx_try_get_instance_field_cached`, `elx_set_instance_field_cached`) so monomorphic sites reuse cached `(shape, slot)` pairs instead of calling into the generic lookup on every iteration.
- Native built-ins such as `clock` and `readLine` now use the dedicated `ObjNative` representation, so `function/print.lox` observes `<native fn>` and built-ins call into the runtime helpers without arity mismatches.
- Runtime debug logging that previously polluted stderr (`call_function`, global getter/setter traces) has been removed so regression fixtures see clean output.
- Block-scoped function declarations allocate and reuse lexical storage, allowing self-recursive locals (`function/local_recursion.lox`) to succeed while keeping `function/local_mutual_recursion.lox` in the expected error state.
- The REPL and file runner align each module with the LLJIT data layout/target triple before submission, and the optimisation layer now runs LLVM’s per-module `-O3` pipeline under a configurable `ELOXIR_DISABLE_OPT` escape hatch.
- Superclass validation rejects `nil`, causing `inheritance/inherit_from_nil.lox` to emit “Superclass must be a class.” as expected.
- Resolver/code-generation updates honour lexical shadowing ahead of upvalues, which fixed the closure fixtures (`closure/shadow_closure_with_local.lox`, `for/closure_in_body.lox`) and restored the expected `for/syntax.lox` output ordering.
- Loop lowering now enforces the Crafting Interpreters 65 535-instruction ceiling and surfaces `CompileError("Loop body too large.")`, satisfying both the official limit test and the accompanying regression coverage.
- Regression coverage for the resolved issues lives under `eloxir/tests/regression/` and is exercised by the smoke-test `pytest` target.
- Instance allocation now goes through a free-list allocator that reuses both `ObjInstance` objects and their field buffers, trimming repeated heap growth during tight loops and enabling the passing benchmarks above.
- Slow-path function calls now respect the call-depth guard, so runaway recursion reports “Stack overflow.” instead of crashing the process.
