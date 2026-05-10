"""CLI parser for the Lox orchestrator."""

from __future__ import annotations

import argparse
from collections.abc import Sequence

from . import __version__
from .commands import (
    add_common_test_options,
    cmd_ast,
    cmd_build,
    cmd_clean,
    cmd_bench,
    cmd_doctor,
    cmd_info,
    cmd_list,
    cmd_paths,
    cmd_run,
    cmd_scan,
    cmd_smoke,
    cmd_test,
)
from .registry import IMPLEMENTATIONS


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="lox.py",
        description="Build, run, test, and inspect all Lox implementations.",
    )
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="List known implementations.")
    list_parser.add_argument("--json", action="store_true", help="Emit JSON metadata.")
    list_parser.set_defaults(func=cmd_list)

    info_parser = subparsers.add_parser("info", help="Show implementation details.")
    info_parser.add_argument("impls", nargs="*", choices=sorted(IMPLEMENTATIONS))
    info_parser.add_argument("--json", action="store_true", help="Emit JSON metadata.")
    info_parser.set_defaults(func=cmd_info)

    paths_parser = subparsers.add_parser("paths", help="Print important repository paths.")
    paths_parser.set_defaults(func=cmd_paths)

    build_parser_ = subparsers.add_parser("build", help="Build implementations.")
    build_parser_.add_argument("impls", nargs="*", choices=sorted(IMPLEMENTATIONS))
    build_parser_.set_defaults(func=cmd_build)

    clean_parser = subparsers.add_parser("clean", help="Remove build artifacts.")
    clean_parser.add_argument("impls", nargs="*", choices=sorted(IMPLEMENTATIONS))
    clean_parser.add_argument("--dry-run", action="store_true", help="Only print removals.")
    clean_parser.set_defaults(func=cmd_clean)

    run_parser = subparsers.add_parser("run", help="Run one implementation.")
    run_parser.add_argument("impl", choices=sorted(IMPLEMENTATIONS))
    run_parser.add_argument("script", nargs="?")
    run_parser.add_argument("--scan", action="store_true", help="Dump scanner tokens.")
    run_parser.add_argument(
        "--print-ast", action="store_true", help="Print the canonical AST."
    )
    run_parser.set_defaults(func=cmd_run)

    scan_parser = subparsers.add_parser("scan", help="Dump tokens for one script.")
    scan_parser.add_argument("impl", choices=sorted(IMPLEMENTATIONS))
    scan_parser.add_argument("script")
    scan_parser.set_defaults(func=cmd_scan)

    ast_parser = subparsers.add_parser("ast", help="Print the canonical AST for one script.")
    ast_parser.add_argument("impl", choices=sorted(IMPLEMENTATIONS))
    ast_parser.add_argument("script")
    ast_parser.set_defaults(func=cmd_ast)

    test_parser = subparsers.add_parser("test", help="Run the official Lox tests.")
    test_parser.add_argument("impls", nargs="*", choices=sorted(IMPLEMENTATIONS))
    test_parser.add_argument("--filter", help="Substring or glob under test/.")
    add_common_test_options(test_parser)
    test_parser.set_defaults(func=cmd_test)

    smoke_parser = subparsers.add_parser("smoke", help="Run a fast cross-feature test set.")
    smoke_parser.add_argument("impls", nargs="*", choices=sorted(IMPLEMENTATIONS))
    add_common_test_options(smoke_parser)
    smoke_parser.set_defaults(func=cmd_smoke)

    bench_parser = subparsers.add_parser("bench", help="Run benchmark tests.")
    bench_parser.add_argument("impls", nargs="*", choices=sorted(IMPLEMENTATIONS))
    bench_parser.add_argument("--filter", help="Substring or glob under test/benchmark.")
    add_common_test_options(bench_parser)
    bench_parser.set_defaults(func=cmd_bench)

    doctor_parser = subparsers.add_parser("doctor", help="Check local toolchain paths.")
    doctor_parser.add_argument("impls", nargs="*", choices=sorted(IMPLEMENTATIONS))
    doctor_parser.add_argument(
        "--require-built",
        action="store_true",
        help="Fail if selected implementation binaries are missing.",
    )
    doctor_parser.set_defaults(func=cmd_doctor)

    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)
