# Eloxir Status Report

## Build
- Configured with `cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo` under `eloxir/build`.
- Latest compile succeeded via `cmake --build eloxir/build --target eloxir`.

## Test Matrix
- `pytest eloxir/tests -q` — **pass** (6 regression cases for runtime and compiler edge conditions).
- `python eloxir/tools/run_official_tests.py` — **fails** (258 passed / 7 failed / 265 total). Every failing fixture is a benchmark that times out; see below.

## Outstanding Issues

### 1. Benchmark programs exceed the 10 s harness timeout
- **Symptoms:** All benchmark fixtures (`benchmark/binary_trees.lox`, `instantiation.lox`, `invocation.lox`, `properties.lox`, `string_equality.lox`, `trees.lox`, `zoo_batch.lox`) terminate via the harness timeout instead of producing output.
- **Cause:** `runFile` (and the REPL path) hand LLVM IR directly to ORC without running any optimisation passes, so the generated machine code executes the interpreter-level control flow verbatim and cannot finish within the limit.
- **Repro:** `python eloxir/tools/run_official_tests.py --filter benchmark/*`.

## Additional Notes
- Native built-ins such as `clock` and `readLine` now use the dedicated `ObjNative` representation, so `function/print.lox` observes `<native fn>` and built-ins call into the runtime helpers without arity mismatches.
- Superclass validation rejects `nil`, causing `inheritance/inherit_from_nil.lox` to emit “Superclass must be a class.” as expected.
- Resolver/code-generation updates honour lexical shadowing ahead of upvalues, which fixed the closure fixtures (`closure/shadow_closure_with_local.lox`, `for/closure_in_body.lox`) and restored the expected `for/syntax.lox` output ordering.
- Loop lowering now enforces the Crafting Interpreters 65 535-instruction ceiling and surfaces `CompileError("Loop body too large.")`, satisfying both the official limit test and the accompanying regression coverage.
- Regression coverage for the resolved issues lives under `eloxir/tests/regression/` and is exercised by the smoke-test `pytest` target.
