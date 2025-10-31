# Eloxir Status Report

## Build
- Configured with `cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo` under `eloxir/build`, then compiled with `cmake --build .` (current build artifact: `eloxir/build/eloxir`).

## Test Matrix
- `pytest eloxir/tests eloxir/tools/tests` — **pass** (8 selected unit and regression fixtures).【873d30†L1-L11】
- `python eloxir/tools/run_official_tests.py` — **fails** (252 passed / 13 failed / 265 total).【853f0b†L1-L18】 The failing fixtures are enumerated below with their reproduction commands.

## Outstanding Issues

### 1. Benchmark programs exceed the 10 s harness timeout
- **Symptoms:** All long-running microbenchmarks (`benchmark/binary_trees.lox`, `instantiation.lox`, `invocation.lox`, `properties.lox`, `string_equality.lox`, `trees.lox`, `zoo_batch.lox`) terminate via the harness timeout rather than completing normally.【853f0b†L8-L83】【21a9f6†L1-L68】
- **Cause:** The generated IR still executes the high-level interpreter semantics without any optimisation passes or specialised runtime paths for object allocation, dynamic dispatch, or string comparison, so the 500 k–1 M loop bodies overwhelm the 10 s ceiling.
- **Repro:** `python eloxir/tools/run_official_tests.py --filter benchmark/*`.

### 2. Lexically shadowed closures keep reading the captured upvalue
- **Symptoms:**
  - `closure/shadow_closure_with_local.lox` prints `closure / closure / closure` instead of `closure / shadow / closure`.
  - `for/closure_in_body.lox` prints per-iteration pairs `1/1`, `2/2`, `3/3` instead of re-reading the loop index after the loop terminates (expected `4/1`, `4/2`, `4/3`).【f2a722†L8-L45】
- **Cause:** `visitVariableExpr` and `visitAssignExpr` always service names that appear in `FunctionContext.upvalue_indices` before consulting the lexical `variableStacks`, so a block-local rebinding never overrides the captured slot when a closure exists in the same function.【e75e1c†L685-L704】【cbaa8e†L901-L938】
- **Repro:**
  - `python eloxir/tools/run_official_tests.py --filter closure/shadow_closure_with_local.lox`
  - `python eloxir/tools/run_official_tests.py --filter for/closure_in_body.lox`

### 3. For-loop desugaring mishandles nested blocks
- **Symptom:** `for/syntax.lox` omits the two leading prints inside `bar()` (only `2` is emitted before the loop exits), so the overall output is missing the expected values for that case.【4bc4a6†L10-L38】
- **Cause:** The parser wraps the original body block inside another block when an increment clause is present, but the backend’s block-scoping machinery treats the inner block as a fresh lexical scope and replays the increment before any loop-local statements on subsequent iterations, effectively skipping the first two prints.【1aab6e†L129-L194】
- **Repro:** `python eloxir/tools/run_official_tests.py --filter for/syntax.lox`

### 4. Native functions print like user-defined functions
- **Symptom:** `function/print.lox` prints `<fn clock>` for the builtin `clock` instead of `<native fn>`.【ac1975†L1-L38】
- **Cause:** The value printer only understands `ObjType::FUNCTION` and `ObjType::CLOSURE`; native builtins are constructed as function objects and never tagged specially, so the printer formats them as `<fn name>`.【6362f6†L256-L334】
- **Repro:** `python eloxir/tools/run_official_tests.py --filter function/print.lox`

### 5. Invalid superclasses slip through class declarations
- **Symptom:** `inheritance/inherit_from_nil.lox` succeeds instead of raising “Superclass must be a class.” (expected runtime error exit code 70).【ec5e45†L1-L10】
- **Cause:** `elx_validate_superclass` immediately returns when the candidate is `nil`, so `nil` and other non-class values reach `elx_allocate_class` without emitting a fatal error.【0b0782†L1275-L1303】
- **Repro:** `python eloxir/tools/run_official_tests.py --filter inheritance/inherit_from_nil.lox`

### 6. Loop body size limit is unenforced
- **Symptom:** `limit/loop_too_large.lox` runs to completion and prints thousands of `nil`s instead of producing a compile-time error.【ec5e45†L10-L38】
- **Cause:** Neither the resolver nor `visitWhileStmt`/`visitBlockStmt` maintain the bytecode-style instruction count described in the book, so there is no guard that trips once 65 535 loop body instructions are generated.【d0acce†L1280-L1338】【c82921†L400-L474】
- **Repro:** `python eloxir/tools/run_official_tests.py --filter limit/loop_too_large.lox`

## Additional Notes
- The runtime error prefix duplication has been eliminated: `string/error_after_multiline.lox` now exits with code 70 and a single “Runtime error:” banner.
- Regression coverage for the fixed issues lives in `eloxir/tests/regression/` and is exercised by `pytest` as part of the smoke test suite.【873d30†L5-L11】

