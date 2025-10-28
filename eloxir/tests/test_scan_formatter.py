"""Golden tests for the scanner's token formatter.

These ensure whitespace-sensitive output stays aligned with the Crafting
Interpreters reference implementation. The tests intentionally avoid stripping
trailing spaces so regressions in the formatter are caught automatically.
"""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ELOXIR_BIN = REPO_ROOT / "eloxir" / "build" / "eloxir"
SCANNING_DIR = REPO_ROOT / "test" / "scanning"

EXPECTATION_MARKER = "// expect:"


def _parse_expectations(path: Path) -> list[str]:
    expectations: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        marker_index = line.find(EXPECTATION_MARKER)
        if marker_index == -1:
            continue
        # Preserve all spacing after the marker so the comparison stays strict.
        expectation = line[marker_index + len(EXPECTATION_MARKER) :]
        expectations.append(expectation.lstrip())
    return expectations


def _run_scan(fixture: str) -> list[str]:
    path = SCANNING_DIR / fixture
    result = subprocess.run(
        [str(ELOXIR_BIN), "--scan", str(path)],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.splitlines()


class ScanFormatterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not ELOXIR_BIN.exists():
            raise AssertionError(
                f"Expected built eloxir binary at {ELOXIR_BIN}, but it was not found."
            )

    def test_strings_fixture_matches_expected_tokens(self) -> None:
        expected = _parse_expectations(SCANNING_DIR / "strings.lox")
        actual = _run_scan("strings.lox")
        self.assertEqual(expected, actual)

    def test_identifiers_fixture_matches_expected_tokens(self) -> None:
        expected = _parse_expectations(SCANNING_DIR / "identifiers.lox")
        actual = _run_scan("identifiers.lox")
        self.assertEqual(expected, actual)


if __name__ == "__main__":
    unittest.main()
