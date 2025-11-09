# Eloxir Status Report

## Build
- Configured with `cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo` under `eloxir/build`.
- Latest compile succeeded via `cmake --build eloxir/build --target eloxir`.

## Test Matrix
- `pytest eloxir/tests -q` — **pass** (9 regression cases for runtime and compiler edge conditions).
- `python eloxir/tools/run_official_tests.py --filter benchmark/*` — **fails** (3 passed / 8 failed / 11 total). Eight long-running benchmark fixtures still time out; see below.

## Outstanding Issues

### 1. Benchmark programs exceed the 10 s harness timeout
- **Symptoms:** Eight benchmark fixtures (`benchmark/binary_trees.lox`, `instantiation.lox`, `invocation.lox`, `properties.lox`, `string_equality.lox`, `trees.lox`, `zoo.lox`, `zoo_batch.lox`) still time out under the harness. Only the three short-running microbenchmarks now finish within the 10 s budget.
- **Cause:** Even with pooled storage, the runtime still performs guarded shape transitions and uncached property/method lookups in the hot loops. Deep recursion (`binary_trees`, `trees`) and repeated property dispatch (`invocation`, `properties`, `zoo*`) continue to execute the slow paths.
- **Repro:** `python eloxir/tools/run_official_tests.py --filter benchmark/*`.

## Additional Notes
- Instance layout now relies on `ObjShape` hidden classes and shape transitions instead of mutating per-class `DenseMap` slot tables. Instances share contiguous field storage that is resized when the shape grows, reducing hash lookups during property reads and writes.
- Property equality checks fast-path interned strings by comparing pointers and falling back to a runtime guard (`elx_strings_equal_interned`) that verifies interning before calling into the structural comparator.
- Native built-ins such as `clock` and `readLine` now use the dedicated `ObjNative` representation, so `function/print.lox` observes `<native fn>` and built-ins call into the runtime helpers without arity mismatches.
- Runtime debug logging that previously polluted stderr (`call_function`, global getter/setter traces) has been removed so regression fixtures see clean output.
- Block-scoped function declarations allocate and reuse lexical storage, allowing self-recursive locals (`function/local_recursion.lox`) to succeed while keeping `function/local_mutual_recursion.lox` in the expected error state.
- The REPL and file runner align each module with the LLJIT data layout/target triple before submission, and the optimisation layer now runs LLVM’s per-module `-O3` pipeline under a configurable `ELOXIR_DISABLE_OPT` escape hatch.
- Superclass validation rejects `nil`, causing `inheritance/inherit_from_nil.lox` to emit “Superclass must be a class.” as expected.
- Resolver/code-generation updates honour lexical shadowing ahead of upvalues, which fixed the closure fixtures (`closure/shadow_closure_with_local.lox`, `for/closure_in_body.lox`) and restored the expected `for/syntax.lox` output ordering.
- Loop lowering now enforces the Crafting Interpreters 65 535-instruction ceiling and surfaces `CompileError("Loop body too large.")`, satisfying both the official limit test and the accompanying regression coverage.
- Regression coverage for the resolved issues lives under `eloxir/tests/regression/` and is exercised by the smoke-test `pytest` target.
- Instance allocation now goes through a free-list allocator that reuses both `ObjInstance` objects and their field buffers, trimming repeated heap growth during tight loops and enabling the passing benchmarks above.
