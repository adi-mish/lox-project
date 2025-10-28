"""Unit tests for the official test harness helpers."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import stat

import pytest

MODULE_PATH = Path(__file__).resolve().parents[1] / "run_official_tests.py"
SPEC = importlib.util.spec_from_file_location("run_official_tests", MODULE_PATH)
assert SPEC and SPEC.loader  # pragma: no cover - defensive
run_official_tests = importlib.util.module_from_spec(SPEC)
sys.modules.setdefault(SPEC.name, run_official_tests)
SPEC.loader.exec_module(run_official_tests)

discover_tests = run_official_tests.discover_tests
parse_expectations = run_official_tests.parse_expectations
resolve_binary = run_official_tests.resolve_binary


def test_parse_expectations_handles_nested_comment_markers(tmp_path: Path) -> None:
    source = """\
        // preliminary comment
        //// expect: first line
        //// expect: second line
        //// expect runtime error
    """
    path = tmp_path / "nested_comments.lox"
    path.write_text(source, encoding="utf-8")

    expectations = parse_expectations(path)

    assert expectations.output == "first line\nsecond line"
    assert expectations.exit_code == 70
    assert expectations.check_stdout is True


def test_discover_tests_filter_substring(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    root = tmp_path / "test"
    benchmark_dir = root / "benchmark"
    expressions_dir = root / "expressions"
    benchmark_dir.mkdir(parents=True)
    expressions_dir.mkdir()

    bench_case = benchmark_dir / "loop.lox"
    other_case = expressions_dir / "literal.lox"
    bench_case.write_text("// expect: ok\n", encoding="utf-8")
    other_case.write_text("// expect: ok\n", encoding="utf-8")

    monkeypatch.setattr(run_official_tests, "TEST_DIR", root)

    matches = discover_tests("benchmark")
    assert matches == [bench_case]
    matches_substring = discover_tests("loop")
    assert matches_substring == [bench_case]
    all_tests = discover_tests()
    assert set(all_tests) == {bench_case, other_case}


def test_resolve_binary_prefers_relwithdebinfo(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    build_dir = tmp_path / "eloxir" / "build"
    relwith_dir = build_dir / "RelWithDebInfo"
    release_dir = build_dir / "Release"
    relwith_dir.mkdir(parents=True)
    release_dir.mkdir()

    relwith_bin = relwith_dir / "eloxir"
    release_bin = release_dir / "eloxir"
    relwith_bin.write_text("", encoding="utf-8")
    release_bin.write_text("", encoding="utf-8")
    relwith_bin.chmod(relwith_bin.stat().st_mode | stat.S_IEXEC)
    release_bin.chmod(release_bin.stat().st_mode | stat.S_IEXEC)

    monkeypatch.setattr(run_official_tests, "BUILD_DIR", build_dir)
    monkeypatch.delenv("ELOXIR_BIN", raising=False)
    monkeypatch.delenv("ELOXIR_BUILD_TYPE", raising=False)

    resolved = resolve_binary(None)
    assert resolved == relwith_bin

    relwith_bin.unlink()
    resolved_release = resolve_binary(None)
    assert resolved_release == release_bin
