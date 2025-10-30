# Eloxir Status Report

## Build
- Configured with `cmake .. -G "Unix Makefiles"` and built via `cmake --build .` in `eloxir/build` (see build logs in session history).

## Tests
- `pytest tests` (Python scanner formatter golden tests) — **pass**.
- `python tools/run_official_tests.py` (Crafting Interpreters conformance suite) — **fails**: 3 of 265 fixtures failed (`limit/too_many_locals.lox`, `operator/equals_class.lox`, `operator/equals_method.lox`, `string/error_after_multiline.lox`). Detailed failures below.

## Known Issues

### 1. Local variable slot limit is not enforced inside functions
- **Symptom:** `limit/too_many_locals.lox` executes instead of reporting a compile-time error; runtime output prints hundreds of `nil` entries and exits with code 0.
- **Cause:** Resolver counts locals from zero and only throws when `localCount >= MAX_LOCAL_SLOTS`. The final `var oops;` becomes slot 256 even though only 255 user locals should be allowed (book reserves one slot for internal use), so the guard never fires. (`frontend/Resolver.cpp`, lines 21-37).
- **Repro:** `python tools/run_official_tests.py --filter limit/too_many_locals.lox`.

### 2. Object equality treats every object as a string
- **Symptom:** `operator/equals_class.lox` and `operator/equals_method.lox` report `false` for identity comparisons that should return `true`.
- **Cause:** `CodeGenVisitor::valuesEqual` calls the string-equality helper for all objects instead of dispatching on object type. Non-string objects always compare unequal even when they reference the same heap object. (`codegen/CodeGenVisitor.cpp`, lines 322-366).
- **Repro:**
  - `python tools/run_official_tests.py --filter operator/equals_class.lox`
  - `python tools/run_official_tests.py --filter operator/equals_method.lox`

### 3. Runtime error messages are double-prefixed
- **Symptom:** `string/error_after_multiline.lox` emits `Runtime error: Runtime error: Undefined variable 'err'.`
- **Cause:** The code generator embeds the "Runtime error:" prefix into messages passed to `elx_runtime_error`, which itself prepends the same prefix and `elx_emit_runtime_error` prints it again. (`codegen/CodeGenVisitor.cpp`, lines 410-1040; `runtime/RuntimeAPI.cpp`, lines 1535-1556).
- **Repro:** `python tools/run_official_tests.py --filter string/error_after_multiline.lox`.

