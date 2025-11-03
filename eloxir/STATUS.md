# Eloxir Status Report

## Build
- Configured with `cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo` under `eloxir/build`.
- Latest compile succeeded via `cmake --build eloxir/build --target eloxir`.

## Test Matrix
- `pytest eloxir/tests -q` — **pass** (9 regression cases for runtime and compiler edge conditions).
- `python eloxir/tools/run_official_tests.py` — **fails** (260 passed / 5 failed / 265 total). Five long-running benchmark fixtures still time out; see below.

## Outstanding Issues

### 1. Benchmark programs exceed the 10 s harness timeout
- **Symptoms:** Five benchmark fixtures (`benchmark/binary_trees.lox`, `instantiation.lox`, `string_equality.lox`, `trees.lox`, `zoo_batch.lox`) still time out under the harness. `invocation.lox`, `properties.lox`, `equality.lox`, and the remaining short benchmarks now finish within the 10 s budget after the allocator changes.
- **Cause:** Even with pooled instance/field storage reducing allocation churn, the generated code continues to mirror the high-level object model—shape lookups, deep recursion, and property equality paths dominate runtime. Specialised layouts and inline caches are still required before these programs can complete within the limit.
- **Repro:** `python eloxir/tools/run_official_tests.py --filter benchmark/*`.

## Additional Notes
- Native built-ins such as `clock` and `readLine` now use the dedicated `ObjNative` representation, so `function/print.lox` observes `<native fn>` and built-ins call into the runtime helpers without arity mismatches.
- Runtime debug logging that previously polluted stderr (`call_function`, global getter/setter traces) has been removed so regression fixtures see clean output.
- Block-scoped function declarations allocate and reuse lexical storage, allowing self-recursive locals (`function/local_recursion.lox`) to succeed while keeping `function/local_mutual_recursion.lox` in the expected error state.
- The REPL and file runner align each module with the LLJIT data layout/target triple before submission, and the optimisation layer now runs LLVM’s per-module `-O3` pipeline under a configurable `ELOXIR_DISABLE_OPT` escape hatch.
- Superclass validation rejects `nil`, causing `inheritance/inherit_from_nil.lox` to emit “Superclass must be a class.” as expected.
- Resolver/code-generation updates honour lexical shadowing ahead of upvalues, which fixed the closure fixtures (`closure/shadow_closure_with_local.lox`, `for/closure_in_body.lox`) and restored the expected `for/syntax.lox` output ordering.
- Loop lowering now enforces the Crafting Interpreters 65 535-instruction ceiling and surfaces `CompileError("Loop body too large.")`, satisfying both the official limit test and the accompanying regression coverage.
- Regression coverage for the resolved issues lives under `eloxir/tests/regression/` and is exercised by the smoke-test `pytest` target.
- Instance allocation now goes through a free-list allocator that reuses both `ObjInstance` objects and their field buffers, trimming repeated heap growth during tight loops and enabling the passing benchmarks above.
