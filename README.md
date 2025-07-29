# Crafting Interpreters – my Lox implementations

This repository tracks my attempt to implement the Lox programming language as described in Bob Nystrom’s excellent book *Crafting Interpreters*. What started as a weekend project to type along with the book has turned into a small collection of interpreters written in different languages:

- **jlox**: a straightforward Java tree‑walking interpreter  
- **clox**: a bytecode interpreter written in C (with an optional C++ build)  
- **eloxir**: an experimental C++17 implementation with a JIT backed by LLVM  

## Motivation

The book itself builds both a tree‑walking interpreter and a bytecode VM to teach different implementation techniques.  
- I built the Java interpreter first to get comfortable with the language’s syntax and semantics.  
- Once I felt comfortable, I rewrote the runtime in C to learn more about manual memory management and VM design.  
- Finally, I started tinkering with a C++17 version that leverages LLVM’s ORC JIT to generate native code on the fly.  

There’s also an optional `cpplox` target hidden in the CMake file for those who want to compile the C VM with a C++ compiler.

## Repository layout

```text
lox-project/
├── jlox/     # Java tree‑walking interpreter (Gradle project)
├── clox/     # C bytecode interpreter and optional C++ build (`cpplox`)
├── eloxir/   # Experimental C++17 front‑end, VM and JIT based on LLVM
└── ...
```

---

## jlox – Tree‑walking in Java

The `jlox` directory contains a minimal interpreter written in plain Java. It follows the first half of *Crafting Interpreters*: it tokenizes the input, builds an abstract syntax tree, and then interprets it recursively.

**Build & run** (using the included Gradle wrapper):

```bash
cd jlox
./gradlew run --args="path/to/your.lox"
```

If you prefer to compile manually, the `build.gradle` file specifies Java 21 as the required toolchain and sets `com.craftinginterpreters.lox.Lox` as the entry point.

---

## clox – Bytecode VM in C

`clox` reimplements the language using a stack‑based virtual machine, closely following the second half of Nystrom’s book. It emits bytecode and executes it on a small VM written in C. There’s no external dependency other than a C compiler and CMake.

**Build**:

```bash
cd clox
cmake -B build
cmake --build build
```

This will produce an executable called `clox` in the `build/Release` directory. Running it without arguments will start a REPL; passing a filename will execute a script. The `CMakeLists.txt` also exposes a `BUILD_CPPLOX` option that builds the same VM as a C++ program when set to `ON`.

---

## eloxir – Exploring a JIT with LLVM

The `eloxir` directory contains my ongoing experiment to add ahead‑of‑time compilation to Lox. The front‑end is written in modern C++ and borrows the scanner, parser, and resolver from the earlier interpreters. The back‑end uses LLVM’s ORC JIT to lower the AST to LLVM IR and execute it natively.

> **Work in progress**: At the moment, it can parse and run small expressions, but larger programs expose bugs and missing features.  
> To try it out, you’ll need a recent version of LLVM installed and CMake configured to find it.

---

## Why this repository exists

Learning by doing has always worked best for me. Typing out the code from *Crafting Interpreters* forced me to think about every line and step through the subtle bugs that never make it into the book. Porting the interpreter across languages taught me how different toolchains and memory models influence the design of a language runtime. The C version exposed me to low‑level debugging tools like Valgrind and sanitizers, and the LLVM experiment has given me a new appreciation for compiler pipelines and optimization passes.

If you’re following along with the book, feel free to use this repository as a reference or starting point. Just keep in mind that these implementations reflect my own learning journey; they may contain rough edges or deviations from the text. Bug reports and suggestions are welcome.

---

## Acknowledgements

This work is based on the book *Crafting Interpreters* by Bob Nystrom. The original source code and explanations are available at [craftinginterpreters.com](https://craftinginterpreters.com). Any mistakes or extensions here are my own.
This work is based on the book *Crafting Interpreters* by Bob Nystrom. The original source code and explanations are available at [craftinginterpreters.com](https://craftinginterpreters.com). Any mistakes or extensions here are my own.

