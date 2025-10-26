#!/usr/bin/env python3
"""Run the official Crafting Interpreters test suite against eloxir.

This harness mirrors the behaviour of the upstream runner but targets the
LLVM-based interpreter that lives in this repository. It discovers the tests,
parses the inline expectations, executes each program with a strict timeout,
and reports a concise summary plus per-failure diagnostics that include a
minimal reproducible example.
"""

from __future__ import annotations

import argparse
import dataclasses
import enum
import os
import subprocess
import sys
from pathlib import Path
from typing import List, Tuple


REPO_ROOT = Path(__file__).resolve().parents[2]
TEST_DIR = REPO_ROOT / "test"
ELOXIR_BIN = REPO_ROOT / "eloxir" / "build" / "eloxir"
DEFAULT_TIMEOUT_SECONDS = 10


class FailureKind(enum.Enum):
    CRASH_TIMEOUT = "CRASH/TIMEOUT"
    SEGFAULT = "SEGMENTATION_FAULT"
    ABORT = "ABORT"
    RUNTIME_ERROR = "RUNTIME_ERROR"
    UNEXPECTED_EXIT = "UNEXPECTED_EXIT"
    MISSING_ERROR = "MISSING_ERROR"
    OUTPUT_MISMATCH = "OUTPUT_MISMATCH"
    UNKNOWN = "UNKNOWN"


@dataclasses.dataclass
class TestExpectations:
    output: str
    exit_code: int
    check_stdout: bool


@dataclasses.dataclass
class TestResult:
    path: Path
    category: str
    expectations: TestExpectations
    actual_stdout: str
    actual_stderr: str
    exit_code: int
    passed: bool
    failure_kind: FailureKind | None = None


def discover_tests() -> List[Path]:
    return sorted(TEST_DIR.rglob("*.lox"))


def parse_expectations(path: Path) -> TestExpectations:
    expected_output: List[str] = []
    saw_parse_error = False
    saw_runtime_error = False
    saw_expect_line = False

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        comment_start = raw_line.find("//")
        if comment_start == -1:
            continue
        comment = raw_line[comment_start:].strip()

        if comment.startswith("// expect:"):
            expected_output.append(comment.split("// expect:", 1)[1].strip())
            saw_expect_line = True
        elif comment.lower().startswith("// expect runtime error"):
            saw_runtime_error = True
        elif comment.startswith("// [line") and "Error" in comment:
            saw_parse_error = True
        elif comment.startswith("// Error"):
            saw_parse_error = True
        elif "Error at" in comment and comment.startswith("//"):
            saw_runtime_error = True

    if saw_parse_error:
        exit_code = 65
    elif saw_runtime_error:
        exit_code = 70
    else:
        exit_code = 0

    return TestExpectations("\n".join(expected_output), exit_code, saw_expect_line)


def _normalize_output(stream: str | bytes | None) -> str:
    if stream is None:
        return ""
    if isinstance(stream, bytes):
        return stream.decode("utf-8", errors="replace").rstrip()
    return stream.rstrip()


def run_test(path: Path, timeout: int) -> Tuple[str, str, int, bool]:
    try:
        completed = subprocess.run(
            [str(ELOXIR_BIN), str(path)],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        stdout = _normalize_output(completed.stdout)
        stderr = _normalize_output(completed.stderr)
        code = completed.returncode
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        stdout = _normalize_output(exc.stdout)
        stderr = f"TIMEOUT: Test exceeded {timeout} seconds"
        code = -1
        timed_out = True
    except FileNotFoundError as exc:  # pragma: no cover - defensive
        raise RuntimeError(f"Failed to execute {ELOXIR_BIN}: {exc}") from exc

    return stdout, stderr, code, timed_out


def classify_failure(result: TestResult, timed_out: bool) -> FailureKind:
    if timed_out or result.exit_code == -1:
        return FailureKind.CRASH_TIMEOUT
    if result.exit_code < 0:
        signal = -result.exit_code
        if signal == 11:
            return FailureKind.SEGFAULT
        if signal == 6:
            return FailureKind.ABORT
        return FailureKind.CRASH_TIMEOUT
    if result.exit_code in {134, 252}:  # SIGABRT or abort-like exits
        return FailureKind.ABORT
    if result.exit_code == 139:
        return FailureKind.SEGFAULT
    exp_code = result.expectations.exit_code
    if exp_code == 0 and result.exit_code != 0:
        if result.actual_stderr:
            return FailureKind.RUNTIME_ERROR
        return FailureKind.UNEXPECTED_EXIT
    if exp_code in {65, 70} and result.exit_code == 0:
        return FailureKind.MISSING_ERROR
    if exp_code == result.exit_code and exp_code in {65, 70}:
        return FailureKind.UNKNOWN
    if result.expectations.output != result.actual_stdout:
        return FailureKind.OUTPUT_MISMATCH
    return FailureKind.UNKNOWN


def minimal_repro(path: Path) -> str:
    lines: List[str] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        stripped = raw_line.strip()
        if not stripped or stripped.startswith("//"):
            continue
        lines.append(raw_line)
    return "\n".join(lines)


def run_suite(timeout: int) -> Tuple[List[TestResult], List[TestResult]]:
    if not TEST_DIR.is_dir():
        raise SystemExit(f"Error: Test directory not found at {TEST_DIR}")
    if not (ELOXIR_BIN.exists() and os.access(ELOXIR_BIN, os.X_OK)):
        raise SystemExit(
            f"Error: eloxir binary not found or not executable at {ELOXIR_BIN}"
        )

    tests = discover_tests()
    results: List[TestResult] = []
    failures: List[TestResult] = []

    total = len(tests)
    print(f"Discovered {total} test files")
    print("Running tests...", flush=True)

    for index, test_path in enumerate(tests, 1):
        expectations = parse_expectations(test_path)
        stdout, stderr, code, timed_out = run_test(test_path, timeout)
        passed = False
        if expectations.exit_code == 0:
            passed = code == 0
            if passed and expectations.check_stdout:
                passed = stdout == expectations.output
        else:
            passed = code == expectations.exit_code
            if passed and expectations.check_stdout:
                passed = stdout == expectations.output

        result = TestResult(
            path=test_path,
            category=test_path.parent.name,
            expectations=expectations,
            actual_stdout=stdout,
            actual_stderr=stderr,
            exit_code=code,
            passed=passed,
        )

        if not passed:
            result.failure_kind = classify_failure(result, timed_out)
            failures.append(result)
        results.append(result)
        print(
            f"\r[{index}/{total}] {test_path.name:<40}",
            end="",
            flush=True,
        )

    print()  # newline after progress
    return results, failures


def print_report(results: List[TestResult], failures: List[TestResult]) -> None:
    total = len(results)
    passed = total - len(failures)
    print(f"\nTest Results: {passed} passed, {len(failures)} failed, {total} total\n")

    for result in failures:
        print(f"File: {result.path}")
        print(f"Category: {result.category}")
        print(f"Failure: {result.failure_kind.value if result.failure_kind else 'UNKNOWN'}")
        print("Expected:")
        print(f"  Exit code: {result.expectations.exit_code}")
        if result.expectations.exit_code == 0:
            print("  Stdout:")
            print("  " + (result.expectations.output or "<empty>").replace("\n", "\n  "))
        print("Actual:")
        print(f"  Exit code: {result.exit_code}")
        if result.actual_stdout:
            print("  Stdout:")
            print("  " + result.actual_stdout.replace("\n", "\n  "))
        if result.actual_stderr:
            print("  Stderr:")
            print("  " + result.actual_stderr.replace("\n", "\n  "))
        print("Minimal Repro:")
        print("```lox")
        print(minimal_repro(result.path))
        print("```")
        print()


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT_SECONDS,
        help="Per-test timeout in seconds (default: %(default)s)",
    )
    args = parser.parse_args(argv)

    results, failures = run_suite(timeout=args.timeout)
    print_report(results, failures)
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
