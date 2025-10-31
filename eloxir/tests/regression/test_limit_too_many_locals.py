"""Regression coverage for the local slot limit.

This ensures the interpreter rejects the official Crafting Interpreters
fixture once user-visible locals would overflow the VM's slot budget.
"""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
ELOXIR_BIN = REPO_ROOT / "eloxir" / "build" / "eloxir"
OFFICIAL_FIXTURE = REPO_ROOT / "test" / "limit" / "too_many_locals.lox"


class TooManyLocalsLimitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not ELOXIR_BIN.exists():
            raise AssertionError(
                f"Expected built eloxir binary at {ELOXIR_BIN}, but it was not found."
            )

    def test_official_fixture_reports_compile_time_error(self) -> None:
        result = subprocess.run(
            [str(ELOXIR_BIN), str(OFFICIAL_FIXTURE)],
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(
            65,
            result.returncode,
            msg=(
                "limit/too_many_locals.lox should fail at compile time,"
                " but the interpreter exited with a different status."
            ),
        )
        diagnostics = "\n".join(part for part in (result.stdout, result.stderr) if part)
        self.assertIn("Too many local variables in function.", diagnostics)


if __name__ == "__main__":
    unittest.main()
