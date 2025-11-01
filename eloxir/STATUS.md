# Eloxir Status Report

## Build
- Configure succeeds with `cmake -S eloxir -B eloxir/build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo`, but the subsequent `cmake --build eloxir/build` step fails in `RuntimeAPI.cpp` before any executable is produced. The compiler reports hundreds of template and overload errors (e.g. duplicate `ObjType::NATIVE` switch labels, `ObjNative` lacking an `arity` field, and mismatched `NativeFn` signatures) so the build never reaches the link step.【55fb28†L1-L200】

## Test Matrix
- `pytest eloxir/tests -q` — **fails** during fixture setup because `eloxir/build/eloxir` is missing after the build failure.【a5c431†L1-L63】
- `python eloxir/tools/run_official_tests.py` — **fails** immediately with the same missing-binary error, so no language fixtures execute.【c486ab†L1-L2】

## Outstanding Issues

### 1. Runtime API header regressions block compilation
- **Symptoms:** `cmake --build eloxir/build` emits type errors for virtually every runtime helper: `allocated_objects.insert(...)` is rejected, native builtins cannot be registered, and switch statements on `ObjType` hit duplicate `case ObjType::NATIVE`.【55fb28†L1-L200】
- **Root cause:** `RuntimeAPI.h` contains two contradictory declarations of `NativeFn` and `ObjNative`, along with a duplicated `ObjType::NATIVE` enumerator. The first definition exposes a `(int, const uint64_t*)` signature with no arity field, while the second expects `(uint64_t*, int)` plus an `int arity`. Translation units that include only the first definition see an `ObjNative` without `arity`, which causes the compile errors when `RuntimeAPI.cpp` references `native->arity` or passes lambdas with the newer calling convention.【F:eloxir/runtime/RuntimeAPI.h†L11-L88】
- **Repro:** `cmake --build eloxir/build` from a clean tree.

### 2. NaN-boxed pointer handling can abort on valid addresses
- **Symptoms:** Any heap address whose canonical value exceeds 48 bits triggers `std::abort()` when converted to a `Value`, which would crash the runtime even before hitting the garbage collector. Modern Linux allocators can legitimately hand out such pointers under ASLR.
- **Root cause:** `Value::object` promises to “use a global pointer table” for wide addresses but never implements it; it simply aborts once the pointer exceeds `0xFFFFFFFFFFFFULL`.【F:eloxir/runtime/Value.h†L31-L53】
- **Repro:** Build a simple native test harness that allocates an object above the 48-bit threshold (e.g. via ASLR tweaking) and call `Value::object(ptr)`.

### 3. Official benchmark timeout diagnosis blocked pending a successful build
- **Status:** The historical benchmark timeout issue (lack of ORC optimisation passes) is still suspected, but the current build failure prevents re-running `python eloxir/tools/run_official_tests.py --filter benchmark/*` to confirm or collect fresh logs.【F:eloxir/tools/repl.cpp†L431-L465】 Until the runtime headers compile, the harness cannot execute any benchmarks.

