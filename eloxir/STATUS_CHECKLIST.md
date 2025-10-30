# Eloxir Stabilization Checklist

## Test Snapshot
- [x] `python eloxir/tools/run_official_tests.py --timeout 10` (184 passed, 81 failed, 265 total).【F:test_results.txt†L1-L19】【F:test_results.txt†L5-L17】

## Critical Issue Clusters

### 1. Object system is unimplemented
- Evidence: every `class`, `constructor`, `field`, `inheritance`, `method`, `operator`, `return/in_method`, `super`, `this`, and related regression test fails with undefined-class lookups or "Can only call functions and closures." runtime traps.【F:test_results.txt†L574-L631】【F:test_results.txt†L1003-L1667】【F:test_results.txt†L4493-L4663】
- Root cause: the code generator stubs out property access, `this`, `super`, and class statements, and the runtime only recognises strings, functions, closures, and upvalues.【F:eloxir/codegen/CodeGenVisitor.cpp†L1802-L1806】【F:eloxir/runtime/RuntimeAPI.h†L10-L40】
- Tasks:
  - [ ] Design and implement runtime representations for classes, instances, bound methods, and superclasses (object layout, allocation helpers, method tables).
  - [ ] Generate IR for `Get`, `Set`, `This`, `Super`, and `Class` nodes, including initializer dispatch and method binding semantics.
  - [ ] Update resolver/codegen coordination so property errors are reported at runtime (invalid receivers) and `super` uses the resolved superclass chain.
  - [ ] Extend call thunks to handle class invocation (constructors) and bound methods, ensuring correct argument validation and error messages.

### 2. Closure capture snapshots values instead of sharing storage
- Evidence: closures that mutate captured locals or rely on loop capture semantics print stale values (e.g., `assign_to_closure.lox`, `for/closure_in_body.lox`).【F:test_results.txt†L704-L742】【F:test_results.txt†L1656-L1689】
- Root cause: `captureUpvalue` allocates new upvalue objects with immediate value copies, severing links to the original slots and upvalue chain.【F:eloxir/codegen/CodeGenVisitor.cpp†L2008-L2088】
- Tasks:
  - [ ] Reintroduce shared upvalue storage (allocate `ObjUpvalue` pointing at the variable slot until closed) instead of `elx_allocate_upvalue_with_value` snapshots.
  - [ ] Track open upvalues per function and close them when locals go out of scope, mirroring Crafting Interpreters semantics.
  - [ ] Audit resolver/local slot bookkeeping so loop variables, shadowed locals, and method parameters map to consistent storage across closures.
  - [ ] Add regression tests covering nested loops, shadowing, and method-closure interactions.

### 3. Compiler resource limits are unenforced
- Evidence: all `limit/**` fixtures run instead of emitting compile-time errors for too many locals/constants/upvalues, and oversize argument lists are accepted until runtime.【F:test_results.txt†L3988-L4110】
- Root cause: `visitVarStmtWithExecution`, function declaration, and call sites never check Crafting Interpreters' constant/local/upvalue caps, nor do they surface arity errors before codegen.【F:eloxir/codegen/CodeGenVisitor.cpp†L1188-L1233】
- Tasks:
  - [ ] Track per-function counters for locals, upvalues, and constants; raise parser/resolver errors when limits are exceeded.
  - [ ] Enforce the 255-argument limit in `finishCall` by converting the diagnostic into a fatal compile error bubbled to the driver.
  - [ ] Ensure `runFile()` returns exit code 65 on these violations and add targeted regression programs.

### 4. Expression-tree mode missing
- Evidence: `expressions/evaluate.lox` and `expressions/parse.lox` expect AST printing but eloxir raises parse errors due to missing statement terminators.【F:test_results.txt†L971-L1001】
- Root cause: there is no CLI flag or code path that mirrors the expression-chapter interpreter; the driver always parses full statements requiring semicolons.【F:eloxir/frontend/Parser.cpp†L207-L214】
- Tasks:
  - [ ] Provide an AST-printing mode (e.g., `--print-ast`) that consumes a single expression and pretty-prints the tree like the book's tool.
  - [ ] Teach the test harness to invoke that mode for `test/expressions/**` files.
  - [ ] Document mode limitations to avoid confusing it with normal execution.

### 5. Scanner output formatting diverges
- Evidence: `scanning/strings.lox` reports an output mismatch because empty string tokens leave a trailing space in the emitted line.【F:test_results.txt†L4452-L4471】
- Root cause: `scanFile()` always prints `token.getLexeme()` followed by a space, even when the literal is empty, so the first line ends with whitespace.【F:eloxir/tools/repl.cpp†L136-L170】
- Tasks:
  - [ ] Suppress the trailing literal separator when the literal string is empty.
  - [ ] Mirror Crafting Interpreters' token dump formatting (lexeme quoted, literal absent when not present) and add output-focused tests.

### 6. Expectation parser misses nested `// expect runtime error` comments
- Evidence: `string/error_after_multiline.lox` should expect a runtime error but the harness treats it as success because the comment is written `// // expect runtime error...` in the fixture.【F:test_results.txt†L4473-L4491】【F:test/string/error_after_multiline.lox†L1-L6】
- Root cause: `parse_expectations()` only matches comments that begin exactly with `// expect runtime error`, ignoring cases with nested comment markers.【F:eloxir/tools/run_official_tests.py†L63-L80】
- Tasks:
  - [ ] Normalise comment strings (strip repeated leading `//`) before matching expectation directives.
  - [ ] Add unit tests for mixed comment styles to avoid future misclassification.

### 7. Super-call diagnostics are generic
- Evidence: `super/extra_arguments.lox` exits with code 70 but emits "Can only call functions and closures." instead of the expected arity message.【F:test_results.txt†L4641-L4663】
- Root cause: with no class infrastructure, super-call resolution fails before reaching arity validation and falls back to the generic call error path.【F:eloxir/codegen/CodeGenVisitor.cpp†L1149-L1156】
- Tasks:
  - [ ] Once classes are implemented, ensure super calls resolve to bound methods before argument count checks.
  - [ ] Enhance runtime error reporting to include callee metadata (name, arity) for clearer diagnostics.

### 8. Benchmark programs time out under the 10s budget
- Evidence: All six benchmark cases hit the harness timeout instead of completing.【F:test_results.txt†L7-L208】
- Root cause: Debug builds and the current runtime dispatch make the JIT slower than the Crafting Interpreters baseline, so long-running stress tests exceed 10 seconds.
- Tasks:
  - [ ] Profile the hot loops (method dispatch, property access) once object support lands and identify optimisation targets.
  - [ ] Provide a Release/RelWithDebInfo build configuration in CI guidance, or tune the timeout heuristics when measuring benchmarks.
  - [ ] Consider adding simple micro-benchmarks to track progress after optimisations.

## Follow-up Tracking
- [ ] Re-run the official suite after each major subsystem fix and update this checklist with the remaining failures.
- [ ] Capture new minimal repro cases for any regressions that appear while addressing the above clusters.
