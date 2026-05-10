# Lox Implementations

This repository contains four implementations of the Lox language from
Robert Nystrom's *Crafting Interpreters*:

- `jlox`: Java tree-walking interpreter.
- `clox`: C bytecode VM, based on the final reference implementation from
  `munificent/craftinginterpreters`.
- `cpplox`: C++20 bytecode VM focused on fast execution and optional VM
  instrumentation.
- `eloxir`: C++17 LLVM ORC JIT implementation.

The official Lox tests live in `test/`. Use the root `lox.py` command, or the
installed `lox` console script, to build, run, test, inspect, and clean all
implementations from one place.

## Quick Start

List implementations:

```bash
./lox.py list
```

Set up the Python orchestration environment:

```bash
uv sync
.venv/bin/lox --version
```

Build everything:

```bash
./lox.py build
```

Run a script:

```bash
./lox.py run jlox test/benchmark/fib.lox
./lox.py run clox test/benchmark/fib.lox
./lox.py run cpplox test/benchmark/fib.lox
./lox.py run eloxir test/benchmark/fib.lox
```

Run the official suite:

```bash
./lox.py test
```

Useful test options:

```bash
./lox.py test jlox clox --filter inheritance
./lox.py test eloxir --skip-build --timeout 10
./lox.py test --show-skips
./lox.py bench clox cpplox --skip-build --timings 12
```

## Python Orchestrator Project

The orchestrator is a small Python project:

```text
lox.py                 # stable executable wrapper
pyproject.toml         # package metadata and console script
uv.lock                # reproducible uv lockfile
loxctl/
  cli.py               # argparse command tree
  commands.py          # command handlers
  registry.py          # implementation registry
  suite.py             # official test runner
  expectations.py      # inline expectation parser
  processes.py         # build, clean, executable resolution
  models.py            # typed dataclasses
  paths.py             # repository path constants
```

`lox.py` stays as the zero-setup entry point. `uv sync` creates `.venv/` and
installs the package in editable mode, making the `lox` console command
available:

```bash
uv sync
.venv/bin/lox list
python -m loxctl list
```

No third-party runtime libraries are needed; the project metadata exists to
make the tool reproducible, installable, and easy to grow.

The registry is the extension point. Each implementation has:

- a name and description,
- build commands,
- executable candidates,
- clean paths,
- capability flags such as `scan` and `print-ast`,
- implementation-specific official-test skips.

To add another implementation, add one `Implementation(...)` entry to the
`IMPLEMENTATIONS` map in `loxctl/registry.py`.

Supported commands:

```bash
./lox.py list
./lox.py info [jlox|clox|cpplox|eloxir ...]
./lox.py paths
./lox.py build [--stats] [jlox|clox|cpplox|eloxir ...]
./lox.py clean [jlox|clox|cpplox|eloxir ...]
./lox.py run <impl> [--scan|--print-ast] [script]
./lox.py run cpplox --stats [script]
./lox.py scan <impl> <script>
./lox.py ast <impl> <script>
./lox.py test [jlox|clox|cpplox|eloxir ...]
./lox.py smoke [jlox|clox|cpplox|eloxir ...]
./lox.py bench [jlox|clox|cpplox|eloxir ...]
./lox.py doctor [jlox|clox|cpplox|eloxir ...]
```

The runner parses the official inline expectations, executes each `.lox` file
in a fresh process, checks exit codes, stdout, and expected error fragments,
and gives benchmark tests a longer timeout. `jlox` and `clox` use canonical
Crafting Interpreters diagnostics, so stderr fragments are checked for them.
`eloxir` currently reports equivalent parse/runtime categories with different
wording, so the orchestrator checks its stdout and exit status while leaving
exact diagnostic text as an implementation-specific surface.

Common workflows:

```bash
# Check local prerequisites and known artifact paths.
./lox.py doctor

# Build only one implementation.
./lox.py build clox

# Run a quick representative test set.
./lox.py smoke

# Run only benchmark programs and print the slowest cases.
./lox.py bench clox cpplox eloxir --skip-build --timings 10
./lox.py bench clox --filter string_equality --skip-build

# Force implementation-specific skipped tests to run as failures.
./lox.py test jlox --strict

# Print machine-readable implementation metadata.
./lox.py info --json
```

## jlox

`jlox` is a Java tree-walking interpreter under:

```text
jlox/src/main/java/com/craftinginterpreters/lox
```

Build directly:

```bash
cd jlox
./gradlew --no-daemon installDist
```

Run directly:

```bash
./jlox/build/install/jlox/bin/jlox path/to/script.lox
```

The project targets Java 17 so the generated install scripts run on the local
JDK. It also exposes test harness modes:

```bash
./lox.py run jlox --scan test/scanning/numbers.lox
./lox.py run jlox --print-ast test/expressions/parse.lox
```

Expected official-suite skips: VM hard-limit tests that do not apply to the
Java tree-walking implementation.

## clox

`clox` is the C bytecode VM. The implementation was replaced with the final
reference C source from the official `munificent/craftinginterpreters`
repository, then kept compatible with this repo's CMake and test harness.

Build directly:

```bash
cmake -S clox -B clox/build -DCMAKE_BUILD_TYPE=Release
cmake --build clox/build
```

Run directly:

```bash
./clox/build/Release/clox path/to/script.lox
```

The root orchestrator also uses a scanner dump mode:

```bash
./lox.py run clox --scan test/scanning/keywords.lox
```

Expected official-suite skips: expression AST-printer chapter tests, because
final `clox` is a bytecode VM and does not expose the Java AST printer.

## cpplox

`cpplox` is a C++20 bytecode VM under `cpplox/`. It keeps Lox semantics aligned
with `clox`, but uses a cleaner C++ build, nan-boxed values, local-slot
single-byte opcodes, direct stack hot paths, and a per-constant global inline
cache.

Build directly:

```bash
cmake -S cpplox -B cpplox/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpplox/build
```

Run directly:

```bash
./cpplox/build/Release/cpplox path/to/script.lox
```

Build and run the instrumented VM through the orchestrator:

```bash
./lox.py build --stats cpplox
./lox.py run cpplox --stats test/benchmark/fib.lox
```

The stats build reports instruction counts, max stack depth, allocation bytes,
call counts, opcode histograms, and global-cache hit/miss counts on stderr.

Expected official-suite skips: expression AST-printer chapter tests, because
`cpplox` is a bytecode VM and does not expose the Java AST printer.

## eloxir

`eloxir` is a C++17 implementation backed by LLVM ORC JIT. It has its own
CMake project under `eloxir/`.

Build directly:

```bash
cmake -S eloxir -B eloxir/build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DELOXIR_ENABLE_CACHE_STATS=OFF
cmake --build eloxir/build
```

Run directly:

```bash
./eloxir/build/eloxir path/to/script.lox
```

Cache-stat instrumentation is disabled by default for optimized normal builds.
Enable it explicitly with `-DELOXIR_ENABLE_CACHE_STATS=ON` when testing cache
counters.

## Current Verification

Recent verification from the root orchestrator:

```text
jlox: 259 passed, 0 failed, 6 skipped, 265 total
clox: 263 passed, 0 failed, 2 skipped, 265 total
cpplox: 263 passed, 0 failed, 2 skipped, 265 total
eloxir: 265 passed, 0 failed, 265 total
```

The skips are implementation-surface differences described above, not known
semantic failures.

Recent benchmark comparison:

```text
clox: 11 passed, 0 failed, 0 skipped, 11 total (20.638s)
cpplox: 11 passed, 0 failed, 0 skipped, 11 total (19.998s)
```

## Acknowledgements

The language, tests, and reference `jlox`/`clox` designs come from
*Crafting Interpreters* by Robert Nystrom:

https://craftinginterpreters.com

Official source repository:

https://github.com/munificent/craftinginterpreters
