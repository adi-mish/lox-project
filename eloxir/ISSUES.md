# Outstanding test gaps and root cause notes

This document captures the remaining discrepancies between `eloxir` and the official Lox test suite. Each section lists an isolated reproducer, the observed failure, and the suspected root cause.

## Benchmark suite timeouts
The throughput-oriented benchmarks still exceed the 5s harness timeout when run against the current JIT/runtime (`binary_trees.lox`, `fib.lox`, `instantiation.lox`, `invocation.lox`, `method_call.lox`, `properties.lox`, `string_equality.lox`, `trees.lox`, `zoo.lox`, `zoo_batch.lox`).【988dda†L1-L125】【988dda†L186-L246】 The harness reports `TIMEOUT` for each case even though they perform only pure computation and object manipulation.

**Minimal reproducer (binary trees):**
```lox
var minDepth = 4;
var maxDepth = 14;
var stretchDepth = maxDepth + 1;
var start = clock();
print "stretch tree of depth:";
print stretchDepth;
print "check:";
print Tree(0, stretchDepth).check();
// ... builds many Tree instances and measures elapsed time
print clock() - start;
```
(from `test/benchmark/binary_trees.lox`).【F:test/benchmark/binary_trees.lox†L1-L71】

**Root cause analysis:**
- Hot loops in these programs repeatedly allocate classes, invoke methods, and traverse object graphs, but the compiled call slow paths (`elx_call_function`/`elx_call_value`) execute without any specialised inlining or caching for repeated receivers. Every call funnels through the generic dispatch machinery, keeping per-call overhead high enough to swamp the benchmark bodies at 5s.【F:eloxir/runtime/RuntimeAPI.cpp†L1261-L1304】
- Property reads/writes and native calls are also dispatched through generic runtime helpers, so the JIT spends more time marshalling arguments and performing tag checks than executing the benchmark logic. Until the JIT emits specialised stubs for monomorphic call sites and cached field accesses, these tight loops cannot meet the suite’s timing expectations.

## Native function printing mismatch
`function/print.lox` expects native functions to render as `<native fn>`, but `clock` prints its symbol name (`<native fn clock>`), causing an output diff.【e58f63†L9-L24】 The printer emits the native’s stored name when present.

**Minimal reproducer:**
```lox
fun foo() {}
print foo; // expect: <fn foo>
print clock; // expect: <native fn>
```
(from `test/function/print.lox`).【F:test/function/print.lox†L1-L4】

**Root cause analysis:**
- In the runtime’s object printer, the native branch checks for a non-empty name and includes it in the formatted string, diverging from the spec’s anonymous native formatting.【F:eloxir/runtime/RuntimeAPI.cpp†L972-L979】

## Stack overflow handling
`limit/stack_overflow.lox` should report a runtime error, but the process segfaults because recursive calls bypass any depth guard.【16ced0†L9-L24】

**Minimal reproducer:**
```lox
fun foo() {
  var a1; var a2; // ... var a16;
  foo(); // expect runtime error: Stack overflow.
}
foo();
```
(from `test/limit/stack_overflow.lox`).【F:test/limit/stack_overflow.lox†L1-L21】

**Root cause analysis:**
- The slow-path entry `elx_call_function` directly invokes the compiled function pointer without checking or incrementing a call-depth counter. Unbounded recursion therefore grows the C++ stack until it crashes instead of surfacing a managed `Stack overflow.` runtime error.【F:eloxir/runtime/RuntimeAPI.cpp†L1261-L1304】
