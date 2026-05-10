"""Official test discovery and execution."""

from __future__ import annotations

import fnmatch
import subprocess
from pathlib import Path

from .expectations import normalize_output, parse_expectations
from .models import FailureKind, Implementation, SuiteReport, TestResult
from .paths import BENCHMARK_TIMEOUT_SECONDS, REPO_ROOT, TEST_DIR
from .processes import resolve_executable


def discover_tests(filter_pattern: str | None = None) -> list[Path]:
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


def command_for_test(impl: Implementation, executable: Path, path: Path) -> list[str]:
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
            command_for_test(impl, executable, path),
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

    if impl.checks_stderr_fragments:
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


def run_test_paths(
    impl: Implementation,
    tests: list[Path],
    timeout: int,
    strict: bool,
) -> SuiteReport:
    executable = resolve_executable(impl)
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

    report = SuiteReport(
        implementation=impl,
        results=tuple(results),
        failures=tuple(failures),
        skipped=tuple(skipped),
    )
    print(
        f"{impl.name}: {report.passed_count} passed, {len(report.failures)} failed, "
        f"{len(report.skipped)} skipped, {len(report.results)} total"
    )
    for failure in report.failures:
        print_failure(failure)
    return report


def run_suite(
    impl: Implementation,
    filter_pattern: str | None,
    timeout: int,
    strict: bool,
) -> SuiteReport:
    return run_test_paths(
        impl,
        tests=discover_tests(filter_pattern),
        timeout=timeout,
        strict=strict,
    )
