"""Command implementations for the Lox orchestrator CLI."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

from .models import Implementation, SuiteReport, TestResult
from .paths import DEFAULT_TIMEOUT_SECONDS, REPO_ROOT, TEST_DIR
from .processes import build_impl, clean_impl, resolve_executable, which
from .registry import IMPLEMENTATIONS, selected_implementations
from .suite import discover_tests, run_suite, run_test_paths


SMOKE_TESTS = (
    "empty_file.lox",
    "precedence.lox",
    "scanning/numbers.lox",
    "expressions/parse.lox",
    "closure/nested_closure.lox",
    "class/inherited_method.lox",
    "inheritance/inherit_methods.lox",
    "super/call_other_method.lox",
)


def impl_to_dict(impl: Implementation) -> dict[str, object]:
    return {
        "name": impl.name,
        "description": impl.description,
        "capabilities": list(impl.capabilities),
        "checks_stderr_fragments": impl.checks_stderr_fragments,
        "build_steps": [step.display() for step in impl.build_steps],
        "executables": [str(path.relative_to(REPO_ROOT)) for path in impl.executable_candidates],
        "clean_paths": [str(path.relative_to(REPO_ROOT)) for path in impl.clean_paths],
        "default_skips": list(impl.default_skip_patterns),
    }


def cmd_list(args: argparse.Namespace) -> int:
    impls = list(IMPLEMENTATIONS.values())
    if args.json:
        print(json.dumps([impl_to_dict(impl) for impl in impls], indent=2))
        return 0

    for impl in impls:
        suffix = f" ({', '.join(impl.capabilities)})" if impl.capabilities else ""
        print(f"{impl.name}: {impl.description}{suffix}")
    return 0


def cmd_info(args: argparse.Namespace) -> int:
    impls = selected_implementations(args.impls)
    if args.json:
        print(json.dumps([impl_to_dict(impl) for impl in impls], indent=2))
        return 0

    for impl in impls:
        print(f"\n{impl.name}: {impl.description}")
        print(f"  capabilities: {', '.join(impl.capabilities) or 'run'}")
        print(f"  stderr fragments: {'checked' if impl.checks_stderr_fragments else 'exit-code only'}")
        print("  build:")
        for step in impl.build_steps:
            print(f"    ({step.cwd.relative_to(REPO_ROOT)}) $ {step.display()}")
        print("  executables:")
        for path in impl.executable_candidates:
            exists = "exists" if path.exists() else "missing"
            print(f"    {path.relative_to(REPO_ROOT)} [{exists}]")
        if impl.default_skip_patterns:
            print("  default skips:")
            for pattern in impl.default_skip_patterns:
                print(f"    {pattern}")
    return 0


def cmd_paths(_args: argparse.Namespace) -> int:
    print(f"repo: {REPO_ROOT}")
    print(f"tests: {TEST_DIR}")
    for impl in IMPLEMENTATIONS.values():
        print(f"{impl.name}:")
        for candidate in impl.executable_candidates:
            print(f"  executable: {candidate}")
        for clean_path in impl.clean_paths:
            print(f"  clean: {clean_path}")
    return 0


def cmd_build(args: argparse.Namespace) -> int:
    for impl in selected_implementations(args.impls):
        build_impl(impl)
    return 0


def cmd_clean(args: argparse.Namespace) -> int:
    for impl in selected_implementations(args.impls):
        clean_impl(impl, dry_run=args.dry_run)
    return 0


def run_impl(impl: Implementation, script: str | None, scan: bool, print_ast: bool) -> int:
    if scan and print_ast:
        raise SystemExit("Choose only one of --scan or --print-ast.")
    if scan and not impl.supports_scan:
        raise SystemExit(f"{impl.name} does not support --scan")
    if print_ast and not impl.supports_print_ast:
        raise SystemExit(f"{impl.name} does not support --print-ast")

    command = [str(resolve_executable(impl))]
    if scan:
        command.append("--scan")
    if print_ast:
        command.append("--print-ast")
    if script:
        command.append(str(Path(script)))
    completed = subprocess.run(command, cwd=REPO_ROOT)
    return completed.returncode


def cmd_run(args: argparse.Namespace) -> int:
    return run_impl(
        IMPLEMENTATIONS[args.impl],
        script=args.script,
        scan=args.scan,
        print_ast=args.print_ast,
    )


def cmd_scan(args: argparse.Namespace) -> int:
    return run_impl(IMPLEMENTATIONS[args.impl], args.script, scan=True, print_ast=False)


def cmd_ast(args: argparse.Namespace) -> int:
    return run_impl(IMPLEMENTATIONS[args.impl], args.script, scan=False, print_ast=True)


def _print_skips(reports: list[SuiteReport]) -> None:
    skipped: list[TestResult] = [item for report in reports for item in report.skipped]
    if not skipped:
        return
    print("\nSkipped tests:")
    for item in skipped:
        print(f"  {item.path.relative_to(REPO_ROOT)}: {item.skip_reason}")


def cmd_test(args: argparse.Namespace) -> int:
    impls = selected_implementations(args.impls)
    if not args.skip_build:
        for impl in impls:
            build_impl(impl)

    reports = [
        run_suite(
            impl,
            filter_pattern=args.filter,
            timeout=args.timeout,
            strict=args.strict,
        )
        for impl in impls
    ]
    if args.show_skips:
        _print_skips(reports)
    return 1 if any(report.failed for report in reports) else 0


def cmd_smoke(args: argparse.Namespace) -> int:
    impls = selected_implementations(args.impls)
    if not args.skip_build:
        for impl in impls:
            build_impl(impl)

    smoke_paths = [TEST_DIR / relative for relative in SMOKE_TESTS]
    reports = [
        run_test_paths(impl, smoke_paths, timeout=args.timeout, strict=args.strict)
        for impl in impls
    ]
    if args.show_skips:
        _print_skips(reports)
    return 1 if any(report.failed for report in reports) else 0


def cmd_bench(args: argparse.Namespace) -> int:
    impls = selected_implementations(args.impls)
    if not args.skip_build:
        for impl in impls:
            build_impl(impl)

    tests = [
        path
        for path in discover_tests(args.filter or "benchmark")
        if path.parent.name == "benchmark"
    ]
    reports = [
        run_test_paths(
            impl,
            tests=tests,
            timeout=args.timeout,
            strict=args.strict,
        )
        for impl in impls
    ]
    if args.show_skips:
        _print_skips(reports)
    return 1 if any(report.failed for report in reports) else 0


def cmd_doctor(args: argparse.Namespace) -> int:
    failed = False
    print(f"repo: {REPO_ROOT}")
    print(f"tests: {'ok' if TEST_DIR.is_dir() else 'missing'} ({TEST_DIR})")
    if not TEST_DIR.is_dir():
        failed = True
    else:
        count = len(list(TEST_DIR.rglob("*.lox")))
        print(f"test files: {count}")

    for tool in ("cmake", "java", "javac"):
        found = which(tool)
        print(f"{tool}: {found or 'missing'}")
        failed = failed or found is None

    gradlew = REPO_ROOT / "jlox" / "gradlew"
    print(f"jlox gradlew: {'ok' if gradlew.exists() else 'missing'}")
    failed = failed or not gradlew.exists()

    for impl in selected_implementations(args.impls):
        print(f"\n{impl.name}:")
        for candidate in impl.executable_candidates:
            status = "ok" if candidate.exists() else "missing"
            print(f"  {candidate.relative_to(REPO_ROOT)}: {status}")
        if args.require_built and not any(path.exists() for path in impl.executable_candidates):
            failed = True

    return 1 if failed else 0


def add_common_test_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT_SECONDS,
        help="Per-test timeout in seconds.",
    )
    parser.add_argument(
        "--skip-build", action="store_true", help="Use existing build artifacts."
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Run implementation-specific skipped tests as failures.",
    )
    parser.add_argument(
        "--show-skips", action="store_true", help="Print skipped test names."
    )
