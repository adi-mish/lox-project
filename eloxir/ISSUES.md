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

