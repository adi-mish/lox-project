# Eloxir Status Report

## Build
- Configured with `cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo` under `eloxir/build`, then compiled with `cmake --build .` (current build artifact: `eloxir/build/eloxir`).

## Test Matrix
- `pytest eloxir/tests -q` — **pass** (5 regression fixtures covering runtime edge cases).【0e2a27†L1-L4】
- `python eloxir/tools/run_official_tests.py` — **fails** (256 passed / 9 failed / 265 total).【ac0786†L1-L8】 The failing fixtures are enumerated below with their reproduction commands.

## Outstanding Issues

### 1. Benchmark programs exceed the 10 s harness timeout
- **Symptoms:** All long-running microbenchmarks (`benchmark/binary_trees.lox`, `instantiation.lox`, `invocation.lox`, `properties.lox`, `string_equality.lox`, `trees.lox`, `zoo_batch.lox`) exit via the harness timeout instead of producing output.【ac0786†L1-L19】【274784†L1-L61】
- **Cause:** The generated IR is submitted to ORC without any optimisation passes, so the JIT executes the interpreter-level control flow and allocation paths verbatim; the hot loops overwhelm the 10 s cap.【44cb5f†L424-L464】
- **Repro:** `python eloxir/tools/run_official_tests.py --filter benchmark/*`.

### 2. Native functions print like user-defined functions
- **Symptom:** `function/print.lox` prints `<fn clock>` for the builtin `clock` instead of `<native fn>`.【e8f4ff†L1-L18】
- **Cause:** `elx_initialize_global_builtins` builds natives via `elx_allocate_function`, so the value printer only sees ordinary function objects and renders them as `<fn name>` with no native tag.【bbaaef†L1476-L1492】
- **Repro:** `python eloxir/tools/run_official_tests.py --filter function/print.lox`

### 3. Invalid superclasses slip through class declarations
- **Symptom:** `inheritance/inherit_from_nil.lox` succeeds instead of raising “Superclass must be a class.” (expected runtime error exit code 70).【629128†L1-L12】
- **Cause:** `elx_validate_superclass` immediately returns when the candidate is `nil`, so `nil` and other non-class values reach `elx_allocate_class` without triggering an error.【43a632†L1275-L1305】
- **Repro:** `python eloxir/tools/run_official_tests.py --filter inheritance/inherit_from_nil.lox`

### 4. Loop body size limit is unenforced
- **Symptom:** `limit/loop_too_large.lox` runs to completion and prints thousands of `nil`s instead of producing a compile-time error.【0fb4ac†L1-L29】
- **Cause:** `visitWhileStmt` simply lowers the loop without tracking emitted instruction counts, so the 65 535-instruction ceiling from the bytecode VM is never enforced.【3ee486†L1400-L1508】
- **Repro:** `python eloxir/tools/run_official_tests.py --filter limit/loop_too_large.lox`

## Additional Notes
- Closure shadowing is now correct: both `closure/shadow_closure_with_local.lox` and `for/closure_in_body.lox` pass after the lexical lookup order fixes in `CodeGenVisitor`.【329ba3†L1-L9】【6b88b5†L1-L6】
- `for/syntax.lox` also passes with the updated stack-slot handling, confirming that the loop increment executes in the proper order.【a625a1†L1-L9】
- Regression coverage for the fixed issues lives in `eloxir/tests/regression/` and is exercised by `pytest` as part of the smoke test suite.【0e2a27†L1-L4】

