"""Implementation registry.

Adding a new backend should normally only require adding another
``Implementation`` entry below.
"""

from __future__ import annotations

from collections.abc import Sequence

from .models import Command, Implementation
from .paths import REPO_ROOT, max_parallel_jobs, repo_path


def command(*argv: str, cwd=REPO_ROOT) -> Command:
    return Command(argv=tuple(argv), cwd=cwd)


IMPLEMENTATIONS: dict[str, Implementation] = {
    "jlox": Implementation(
        name="jlox",
        description="Java tree-walking interpreter",
        build_steps=(
            command("./gradlew", "--no-daemon", "installDist", cwd=repo_path("jlox")),
        ),
        executable_candidates=(repo_path("jlox", "build", "install", "jlox", "bin", "jlox"),),
        clean_paths=(repo_path("jlox", "build"),),
        supports_scan=True,
        supports_print_ast=True,
        expectation_marker="java",
        default_skip_patterns=(
            "test/limit/loop_too_large.lox",
            "test/limit/no_reuse_constants.lox",
            "test/limit/stack_overflow.lox",
            "test/limit/too_many_constants.lox",
            "test/limit/too_many_locals.lox",
            "test/limit/too_many_upvalues.lox",
        ),
    ),
    "clox": Implementation(
        name="clox",
        description="C bytecode VM",
        build_steps=(
            command("cmake", "-S", "clox", "-B", "clox/build", "-DCMAKE_BUILD_TYPE=Release"),
            command("cmake", "--build", "clox/build", "-j", max_parallel_jobs()),
        ),
        executable_candidates=(
            repo_path("clox", "build", "Release", "clox"),
            repo_path("clox", "build", "clox"),
        ),
        clean_paths=(repo_path("clox", "build"),),
        supports_scan=True,
        expectation_marker="c",
    ),
    "eloxir": Implementation(
        name="eloxir",
        description="C++17 LLVM ORC JIT implementation",
        build_steps=(
            command(
                "cmake",
                "-S",
                "eloxir",
                "-B",
                "eloxir/build",
                "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                "-DELOXIR_ENABLE_CACHE_STATS=OFF",
            ),
            command("cmake", "--build", "eloxir/build", "-j", max_parallel_jobs()),
        ),
        executable_candidates=(
            repo_path("eloxir", "build", "eloxir"),
            repo_path("eloxir", "build", "RelWithDebInfo", "eloxir"),
            repo_path("eloxir", "build", "Release", "eloxir"),
        ),
        clean_paths=(repo_path("eloxir", "build"),),
        supports_scan=True,
        supports_print_ast=True,
    ),
}


def implementation_names(names: Sequence[str]) -> list[str]:
    return list(names) if names else list(IMPLEMENTATIONS)


def selected_implementations(names: Sequence[str]) -> list[Implementation]:
    return [IMPLEMENTATIONS[name] for name in implementation_names(names)]
