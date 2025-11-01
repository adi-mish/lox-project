"""Regression tests for previously failing runtime semantics."""

from __future__ import annotations

import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "run_official_tests.py"
SPEC = importlib.util.spec_from_file_location("run_official_tests", MODULE_PATH)
assert SPEC and SPEC.loader  # pragma: no cover - defensive
run_official_tests = importlib.util.module_from_spec(SPEC)
sys.modules.setdefault(SPEC.name, run_official_tests)
SPEC.loader.exec_module(run_official_tests)

resolve_binary = run_official_tests.resolve_binary


class RuntimeRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.repo_root = Path(__file__).resolve().parents[2]
        cls.fixtures = cls.repo_root / "eloxir" / "tests" / "regression"
        cls.binary = resolve_binary(None)
        if not cls.binary.exists():
            raise AssertionError(
                f"Expected built eloxir binary at {cls.binary}, but it was not found."
            )

    def _run_fixture(self, name: str) -> subprocess.CompletedProcess:
        path = self.fixtures / name
        result = subprocess.run(
            [str(self.binary), str(path)],
            capture_output=True,
            text=True,
            check=False,
        )
        return result

    def test_too_many_locals_reports_compile_error(self) -> None:
        result = self._run_fixture("too_many_locals.lox")
        self.assertEqual(result.returncode, 65)
        self.assertIn("Too many local variables in function.", result.stderr)

    def test_object_identity_equality_matches_reference_semantics(self) -> None:
        result = self._run_fixture("object_identity_equality.lox")
        self.assertEqual(result.returncode, 0)
        expected_lines = [
            "true",
            "false",
            "false",
            "false",
            "true",
            "false",
            "true",
            "false",
        ]
        actual_lines = [line.strip() for line in result.stdout.splitlines() if line]
        self.assertEqual(actual_lines, expected_lines)
        self.assertEqual(result.stderr.strip(), "")

    def test_plus_with_non_string_objects_raises_type_error(self) -> None:
        result = self._run_fixture("add_non_string_objects.lox")
        self.assertEqual(result.returncode, 70)
        self.assertIn(
            "Operands must be numbers or strings for +.",
            result.stderr,
        )

    def test_loop_too_large_reports_compile_error(self) -> None:
        lines = ["while (false) {"]
        chunk = " ".join(["nil;"] * 256)
        for _ in range(128):
            lines.append(f"  {chunk}")
        lines.append("}")
        source = "\n".join(lines) + "\n"

        with tempfile.NamedTemporaryFile("w", suffix=".lox", delete=False) as tmp:
            tmp.write(source)
            tmp_path = Path(tmp.name)

        try:
            result = subprocess.run(
                [str(self.binary), str(tmp_path)],
                capture_output=True,
                text=True,
                check=False,
            )
        finally:
            os.unlink(tmp_path)

        self.assertEqual(result.returncode, 65)
        self.assertIn("Loop body too large.", result.stderr)


if __name__ == "__main__":
    unittest.main()
