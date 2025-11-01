# Eloxir Status Report

## Build
- Configured with `cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo` under `eloxir/build`, then compiled with `cmake --build .` (current build artifact: `eloxir/build/eloxir`).

## Test Matrix
- `pytest eloxir/tests -q` — **pass** (6 regression fixtures covering runtime and compiler edge cases).【63baea†L1-L3】
- `python eloxir/tools/run_official_tests.py` — **fails** (258 passed / 7 failed / 265 total).【1e1840†L1-L8】 The failing fixtures are enumerated below with their reproduction commands.

## Outstanding Issues

### 1. Benchmark programs exceed the 10 s harness timeout
- **Symptoms:** All long-running microbenchmarks (`benchmark/binary_trees.lox`, `instantiation.lox`, `invocation.lox`, `properties.lox`, `string_equality.lox`, `trees.lox`, `zoo_batch.lox`) exit via the harness timeout instead of producing output.【edf082†L1-L8】【274784†L1-L61】
- **Cause:** The generated IR is submitted to ORC without any optimisation passes, so the JIT executes the interpreter-level control flow and allocation paths verbatim; the hot loops overwhelm the 10 s cap.【F:eloxir/tools/repl.cpp†L431-L465】
- **Repro:** `python eloxir/tools/run_official_tests.py --filter benchmark/*`.

## Additional Notes
- Native built-ins now print as `<native fn>` and reject non-zero arities: both `function/print.lox` and the updated runtime allocate `clock`/`readLine` as `ObjNative` values. 【e91df3†L1-L8】【F:eloxir/runtime/RuntimeAPI.cpp†L1572-L1580】【F:eloxir/runtime/RuntimeAPI.cpp†L727-L776】【F:eloxir/runtime/RuntimeAPI.cpp†L353-L355】
- Class declarations that inherit from `nil` now raise “Superclass must be a class.” thanks to stricter validation and early bailout in `elx_validate_superclass` / `elx_allocate_class`.【80d133†L1-L8】【F:eloxir/runtime/RuntimeAPI.cpp†L1356-L1405】
- Closure shadowing is now correct: both `closure/shadow_closure_with_local.lox` and `for/closure_in_body.lox` pass after the lexical lookup order fixes in `CodeGenVisitor`.【329ba3†L1-L9】【6b88b5†L1-L6】
- `for/syntax.lox` also passes with the updated stack-slot handling, confirming that the loop increment executes in the proper order.【a625a1†L1-L9】
- Loop bodies now enforce the Crafting Interpreters 65 535-instruction ceiling: `visitWhileStmt` estimates per-iteration work and throws `CompileError("Loop body too large.")` when the limit is exceeded, matching both the official fixture and the new regression test.【F:eloxir/codegen/CodeGenVisitor.h†L32-L37】【F:eloxir/codegen/CodeGenVisitor.cpp†L200-L274】【F:eloxir/codegen/CodeGenVisitor.cpp†L1463-L1492】【83b9ea†L1-L8】【def571†L1-L8】
- Regression coverage for the fixed issues lives in `eloxir/tests/regression/` and is exercised by `pytest` as part of the smoke test suite.【63baea†L1-L3】

