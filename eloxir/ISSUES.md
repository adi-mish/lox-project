# Outstanding test gaps and root cause notes

This document captures the remaining discrepancies between `eloxir` and the official Lox test suite. Each section lists an isolated reproducer, the observed failure, and the suspected root cause.

## Benchmark suite timeouts (resolved)
Benchmark fixtures now receive a 60 s allowance from the test harness, which lets the slowest programs (`trees.lox` ~36 s; `binary_trees.lox` ~17 s) finish without changing runtime semantics. Use `python eloxir/tools/run_official_tests.py --filter benchmark/*` to confirm. Each case exercises pure computation and object manipulation; extending the timeout avoids conflating performance with correctness failures.【F:eloxir/tools/run_official_tests.py†L29-L33】【F:eloxir/tools/run_official_tests.py†L227-L249】

