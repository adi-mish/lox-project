"""Regression coverage for inheriting from non-class values.

This ensures the interpreter matches the Crafting Interpreters fixture for
`inheritance/inherit_from_nil.lox` by reporting a runtime error and exit code
70 when attempting to inherit from `nil`.
"""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
ELOXIR_BIN = REPO_ROOT / "eloxir" / "build" / "eloxir"
OFFICIAL_FIXTURE = REPO_ROOT / "test" / "inheritance" / "inherit_from_nil.lox"


class InheritFromNilRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not ELOXIR_BIN.exists():
            raise AssertionError(
                f"Expected built eloxir binary at {ELOXIR_BIN}, but it was not found."
            )

    def test_official_fixture_reports_runtime_error(self) -> None:
        result = subprocess.run(
            [str(ELOXIR_BIN), str(OFFICIAL_FIXTURE)],
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(
            70,
            result.returncode,
            msg=(
                "inheritance/inherit_from_nil.lox should raise a runtime error"
                " with exit code 70, but the interpreter exited differently."
            ),
        )

        diagnostics = "\n".join(part for part in (result.stdout, result.stderr) if part)
        self.assertIn("Superclass must be a class.", diagnostics)


if __name__ == "__main__":
    unittest.main()
