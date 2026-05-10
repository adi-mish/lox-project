#!/usr/bin/env python3
"""Build, run, and test the Lox implementations in this repository."""

from __future__ import annotations

import argparse
import dataclasses
import enum
import fnmatch
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Sequence


REPO_ROOT = Path(__file__).resolve().parent
TEST_DIR = REPO_ROOT / "test"
DEFAULT_TIMEOUT_SECONDS = 10
BENCHMARK_TIMEOUT_SECONDS = 60


class FailureKind(enum.Enum):
    TIMEOUT = "TIMEOUT"
    UNSUPPORTED_MODE = "UNSUPPORTED_MODE"
    EXIT_CODE = "EXIT_CODE"
    STDOUT = "STDOUT"
    STDERR = "STDERR"


@dataclasses.dataclass(frozen=True)
class Implementation:
    name: str
    description: str
    build_commands: tuple[tuple[str | Path, ...], ...]
    executable_candidates: tuple[Path, ...]
    supports_scan: bool = False
    supports_print_ast: bool = False
    default_skip_patterns: tuple[str, ...] = ()


@dataclasses.dataclass(frozen=True)
class Expectations:
    stdout: str
    stderr_fragments: tuple[str, ...]
    exit_code: int
    check_stdout: bool


@dataclasses.dataclass
class TestResult:
    path: Path
    expectations: Expectations
    stdout: str = ""
    stderr: str = ""
    exit_code: int = 0
    passed: bool = False
    skipped: bool = False
    skip_reason: str | None = None
    failure_kind: FailureKind | None = None


def _jobs() -> str:
    return str(max(1, min(os.cpu_count() or 2, 8)))


IMPLEMENTATIONS: dict[str, Implementation] = {
    "jlox": Implementation(
        name="jlox",
        description="Java tree-walking interpreter",
        build_commands=(
            ("./gradlew", "--no-daemon", "installDist"),
        ),
        executable_candidates=(REPO_ROOT / "jlox/build/install/jlox/bin/jlox",),
        supports_scan=True,
        supports_print_ast=True,
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
        build_commands=(
            ("cmake", "-S", "clox", "-B", "clox/build", "-DCMAKE_BUILD_TYPE=Release"),
            ("cmake", "--build", "clox/build", "-j", _jobs()),
        ),
        executable_candidates=(
            REPO_ROOT / "clox/build/Release/clox",
            REPO_ROOT / "clox/build/clox",
        ),
        supports_scan=True,
    ),
    "eloxir": Implementation(
        name="eloxir",
        description="C++17 LLVM ORC JIT implementation",
        build_commands=(
            (
                "cmake",
                "-S",
                "eloxir",
                "-B",
                "eloxir/build",
                "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                "-DELOXIR_ENABLE_CACHE_STATS=OFF",
            ),
            ("cmake", "--build", "eloxir/build", "-j", _jobs()),
        ),
        executable_candidates=(
            REPO_ROOT / "eloxir/build/eloxir",
            REPO_ROOT / "eloxir/build/RelWithDebInfo/eloxir",
            REPO_ROOT / "eloxir/build/Release/eloxir",
        ),
        supports_scan=True,
        supports_print_ast=True,
    ),
}


def implementation_names(names: Sequence[str]) -> list[str]:
    return list(names) if names else list(IMPLEMENTATIONS)


def command_cwd(command: Sequence[str | Path], impl: Implementation) -> Path:
    if impl.name == "jlox" and str(command[0]) == "./gradlew":
        return REPO_ROOT / "jlox"
    return REPO_ROOT


def run_checked(command: Sequence[str | Path], cwd: Path) -> None:
    printable = " ".join(str(part) for part in command)
    print(f"$ {printable}")
    completed = subprocess.run([str(part) for part in command], cwd=cwd)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def build_impl(impl: Implementation) -> None:
    for command in impl.build_commands:
        run_checked(command, command_cwd(command, impl))


def resolve_executable(impl: Implementation) -> Path:
    for candidate in impl.executable_candidates:
        if candidate.exists() and os.access(candidate, os.X_OK):
            return candidate
    candidates = ", ".join(str(path) for path in impl.executable_candidates)
    raise SystemExit(f"No executable found for {impl.name}. Tried: {candidates}")


def discover_tests(filter_pattern: str | None) -> list[Path]:
    tests = sorted(TEST_DIR.rglob("*.lox"))
    if not filter_pattern:
        return tests

    query = filter_pattern.strip()
    if not query:
        return tests

    has_glob = any(ch in query for ch in "*?[]")
    lowered = query.lower()

    def matches(path: Path) -> bool:
        relative = path.relative_to(TEST_DIR).as_posix()
        if has_glob:
            return fnmatch.fnmatch(relative, query)
        return lowered in relative.lower()

    return [path for path in tests if matches(path)]


def _strip_nested_comment_markers(comment: str) -> str:
    text = comment.strip()
    while text.startswith("//"):
        text = text[2:].strip()
    return text


def implementation_marker(impl: Implementation) -> str | None:
    if impl.name == "jlox":
        return "java"
    if impl.name == "clox":
        return "c"
    return None


def parse_expectations(path: Path, impl: Implementation) -> Expectations:
    expected_stdout: list[str] = []
    stderr_fragments: list[str] = []
    saw_parse_error = False
    saw_runtime_error = False
    saw_expect_line = False
    marker = implementation_marker(impl)

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        comment_start = raw_line.find("//")
        if comment_start == -1:
            continue

        comment = raw_line[comment_start:].strip()
        text = _strip_nested_comment_markers(comment)

        expect_match = re.match(r"^expect:\s*(.*)$", text, re.IGNORECASE)
        if expect_match:
            expected_stdout.append(expect_match.group(1).strip())
            saw_expect_line = True
            continue

        runtime_match = re.match(
            r"^expect runtime error:\s*(.*)$", text, re.IGNORECASE
        )
        if runtime_match:
            saw_runtime_error = True
            fragment = runtime_match.group(1).strip()
            if fragment:
                stderr_fragments.append(fragment)
            continue

        marker_match = re.match(r"^\[(java|c) line (\d+)\](.*)$", text)
        if marker_match:
            if marker_match.group(1) == marker:
                saw_parse_error = True
                stderr_fragments.append(f"[line {marker_match.group(2)}]{marker_match.group(3)}")
            continue

        if text.startswith("[line") and "Error" in text:
            saw_parse_error = True
            stderr_fragments.append(text)
        elif text.startswith("Error") or "Error at" in text:
            saw_parse_error = True
            stderr_fragments.append(text)

    if saw_parse_error:
        exit_code = 65
    elif saw_runtime_error:
        exit_code = 70
    else:
        exit_code = 0

    return Expectations(
        stdout="\n".join(expected_stdout),
        stderr_fragments=tuple(stderr_fragments),
        exit_code=exit_code,
        check_stdout=saw_expect_line,
    )


def normalize_output(stream: str | bytes | None) -> str:
    if stream is None:
        return ""
    if isinstance(stream, bytes):
        return stream.decode("utf-8", errors="replace").rstrip()
    return stream.rstrip()


def relative_test_path(path: Path) -> str:
    return "test/" + path.relative_to(TEST_DIR).as_posix()


def should_skip(path: Path, impl: Implementation, strict: bool) -> str | None:
    category = path.parent.name
    if category == "scanning" and not impl.supports_scan:
        return None if strict else "implementation has no scanner dump mode"
    if category == "expressions" and not impl.supports_print_ast:
        return None if strict else "implementation has no AST printer mode"

    relative = relative_test_path(path)
    for pattern in impl.default_skip_patterns:
        if relative == pattern or fnmatch.fnmatch(relative, pattern):
            return None if strict else "implementation-defined limit test"
    return None


def test_command(impl: Implementation, executable: Path, path: Path) -> list[str]:
    command = [str(executable)]
    category = path.parent.name
    if category == "scanning":
        command.append("--scan")
    elif category == "expressions":
        command.append("--print-ast")
    command.append(str(path))
    return command


def timeout_for(path: Path, base_timeout: int) -> int:
    if path.parent.name == "benchmark":
        return max(base_timeout, BENCHMARK_TIMEOUT_SECONDS)
    return base_timeout


def run_one_test(
    impl: Implementation,
    executable: Path,
    path: Path,
    timeout: int,
    strict: bool,
) -> TestResult:
    expectations = parse_expectations(path, impl)
    result = TestResult(path=path, expectations=expectations)
    skip_reason = should_skip(path, impl, strict)
    if skip_reason:
        result.skipped = True
        result.skip_reason = skip_reason
        return result

    category = path.parent.name
    if category == "scanning" and not impl.supports_scan:
        result.failure_kind = FailureKind.UNSUPPORTED_MODE
        return result
    if category == "expressions" and not impl.supports_print_ast:
        result.failure_kind = FailureKind.UNSUPPORTED_MODE
        return result

    try:
        completed = subprocess.run(
            test_command(impl, executable, path),
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=timeout_for(path, timeout),
        )
        result.stdout = normalize_output(completed.stdout)
        result.stderr = normalize_output(completed.stderr)
        result.exit_code = completed.returncode
    except subprocess.TimeoutExpired as exc:
        result.stdout = normalize_output(exc.stdout)
        result.stderr = f"TIMEOUT: exceeded {timeout_for(path, timeout)} seconds"
        result.exit_code = -1
        result.failure_kind = FailureKind.TIMEOUT
        return result

    if result.exit_code != expectations.exit_code:
        result.failure_kind = FailureKind.EXIT_CODE
        return result

    if expectations.check_stdout and result.stdout != expectations.stdout:
        result.failure_kind = FailureKind.STDOUT
        return result

    for fragment in expectations.stderr_fragments:
        if fragment not in result.stderr:
            result.failure_kind = FailureKind.STDERR
            return result

    result.passed = True
    return result


def print_failure(result: TestResult) -> None:
    print(f"\nFile: {result.path.relative_to(REPO_ROOT)}")
    print(f"Failure: {result.failure_kind.value if result.failure_kind else 'UNKNOWN'}")
    print(f"Expected exit: {result.expectations.exit_code}")
    print(f"Actual exit: {result.exit_code}")
    if result.expectations.check_stdout:
        print("Expected stdout:")
        print(result.expectations.stdout or "<empty>")
    if result.expectations.stderr_fragments:
        print("Expected stderr fragments:")
        for fragment in result.expectations.stderr_fragments:
            print(f"  {fragment}")
    if result.stdout:
        print("Actual stdout:")
        print(result.stdout)
    if result.stderr:
        print("Actual stderr:")
        print(result.stderr)


def run_suite(
    impl: Implementation,
    filter_pattern: str | None,
    timeout: int,
    strict: bool,
) -> tuple[list[TestResult], list[TestResult], list[TestResult]]:
    executable = resolve_executable(impl)
    tests = discover_tests(filter_pattern)
    results: list[TestResult] = []
    failures: list[TestResult] = []
    skipped: list[TestResult] = []

    print(f"\n== {impl.name} ==")
    print(f"Using {executable.relative_to(REPO_ROOT)}")
    for index, path in enumerate(tests, start=1):
        result = run_one_test(impl, executable, path, timeout, strict)
        results.append(result)
        if result.skipped:
            skipped.append(result)
        elif not result.passed:
            failures.append(result)
        print(f"\r[{index}/{len(tests)}] {path.relative_to(TEST_DIR)}", end="", flush=True)
    print()

    passed = len(results) - len(failures) - len(skipped)
    print(
        f"{impl.name}: {passed} passed, {len(failures)} failed, "
        f"{len(skipped)} skipped, {len(results)} total"
    )
    for failure in failures:
        print_failure(failure)
    return results, failures, skipped


def cmd_list(_args: argparse.Namespace) -> int:
    for impl in IMPLEMENTATIONS.values():
        capabilities = []
        if impl.supports_scan:
            capabilities.append("scan")
        if impl.supports_print_ast:
            capabilities.append("print-ast")
        suffix = f" ({', '.join(capabilities)})" if capabilities else ""
        print(f"{impl.name}: {impl.description}{suffix}")
    return 0


def cmd_build(args: argparse.Namespace) -> int:
    for name in implementation_names(args.impls):
        build_impl(IMPLEMENTATIONS[name])
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    impl = IMPLEMENTATIONS[args.impl]
    executable = resolve_executable(impl)
    command = [str(executable)]
    if args.scan:
        if not impl.supports_scan:
            raise SystemExit(f"{impl.name} does not support --scan")
        command.append("--scan")
    if args.print_ast:
        if not impl.supports_print_ast:
            raise SystemExit(f"{impl.name} does not support --print-ast")
        command.append("--print-ast")
    if args.script:
        command.append(str(Path(args.script)))
    completed = subprocess.run(command, cwd=REPO_ROOT)
    return completed.returncode


def cmd_test(args: argparse.Namespace) -> int:
    selected = [IMPLEMENTATIONS[name] for name in implementation_names(args.impls)]
    if not args.skip_build:
        for impl in selected:
            build_impl(impl)

    any_failures = False
    all_skipped: list[TestResult] = []
    for impl in selected:
        _, failures, skipped = run_suite(
            impl,
            filter_pattern=args.filter,
            timeout=args.timeout,
            strict=args.strict,
        )
        any_failures = any_failures or bool(failures)
        all_skipped.extend(skipped)

    if args.show_skips and all_skipped:
        print("\nSkipped tests:")
        for skipped in all_skipped:
            print(f"  {skipped.path.relative_to(REPO_ROOT)}: {skipped.skip_reason}")

    return 1 if any_failures else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="List known implementations.")
    list_parser.set_defaults(func=cmd_list)

    build_parser_ = subparsers.add_parser("build", help="Build implementations.")
    build_parser_.add_argument("impls", nargs="*", choices=sorted(IMPLEMENTATIONS))
    build_parser_.set_defaults(func=cmd_build)

    run_parser = subparsers.add_parser("run", help="Run one implementation.")
    run_parser.add_argument("impl", choices=sorted(IMPLEMENTATIONS))
    run_parser.add_argument("script", nargs="?")
    run_parser.add_argument("--scan", action="store_true", help="Dump scanner tokens.")
    run_parser.add_argument(
        "--print-ast", action="store_true", help="Print the canonical AST."
    )
    run_parser.set_defaults(func=cmd_run)

    test_parser = subparsers.add_parser("test", help="Run the official Lox tests.")
    test_parser.add_argument("impls", nargs="*", choices=sorted(IMPLEMENTATIONS))
    test_parser.add_argument("--filter", help="Substring or glob under test/.")
    test_parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT_SECONDS,
        help="Per-test timeout in seconds.",
    )
    test_parser.add_argument(
        "--skip-build", action="store_true", help="Use existing build artifacts."
    )
    test_parser.add_argument(
        "--strict",
        action="store_true",
        help="Run implementation-specific skipped tests as failures.",
    )
    test_parser.add_argument(
        "--show-skips", action="store_true", help="Print skipped test names."
    )
    test_parser.set_defaults(func=cmd_test)

    return parser


def main(argv: Sequence[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
