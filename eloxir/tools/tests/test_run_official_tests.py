"""Unit tests for the official test harness helpers."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[1] / "run_official_tests.py"
SPEC = importlib.util.spec_from_file_location("run_official_tests", MODULE_PATH)
assert SPEC and SPEC.loader  # pragma: no cover - defensive
run_official_tests = importlib.util.module_from_spec(SPEC)
sys.modules.setdefault(SPEC.name, run_official_tests)
SPEC.loader.exec_module(run_official_tests)

parse_expectations = run_official_tests.parse_expectations


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
