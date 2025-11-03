from __future__ import annotations

import importlib.util
import json
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


class CacheStatsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.repo_root = Path(__file__).resolve().parents[2]
        cls.binary = resolve_binary(None)
        if not cls.binary.exists():
            raise AssertionError(
                f"Expected built eloxir binary at {cls.binary}, but it was not found."
            )

    def _run_with_cache_stats(self, source: str) -> tuple[subprocess.CompletedProcess, dict[str, object]]:
        with tempfile.NamedTemporaryFile("w", suffix=".lox", delete=False) as tmp:
            tmp.write(source)
            tmp_path = Path(tmp.name)

        try:
            result = subprocess.run(
                [str(self.binary), "--cache-stats", str(tmp_path)],
                capture_output=True,
                text=True,
                check=False,
            )
        finally:
            try:
                os.unlink(tmp_path)
            except FileNotFoundError:  # pragma: no cover - best effort cleanup
                pass

        stats_line = None
        for line in reversed(result.stdout.splitlines()):
            if line.startswith("CACHE_STATS "):
                stats_line = line
                break

        if stats_line is None:
            self.fail(
                "Expected cache statistics marker in stdout. Got:\n" + result.stdout
            )

        payload = stats_line.split(" ", 1)[1]
        stats = json.loads(payload)
        return result, stats

    def _require_enabled(self, stats: dict[str, object]) -> None:
        enabled = stats.get("enabled")
        if not enabled:
            self.skipTest("Cache statistics instrumentation disabled in this build")

    def test_property_cache_monomorphic_counters(self) -> None:
        script = """
        class Box {
          init(value) {
            this.value = value;
          }
        }

        var box = Box(1);
        var total = 0;
        for (var i = 0; i < 16; i = i + 1) {
          total = total + box.value;
        }
        print total;
        """

        result, stats = self._run_with_cache_stats(script)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self._require_enabled(stats)

        self.assertEqual(stats.get("property_get_shape_transitions"), 1)
        self.assertEqual(stats.get("property_get_misses"), 16)
        self.assertEqual(stats.get("property_set_shape_transitions"), 1)
        self.assertEqual(stats.get("property_set_misses"), 1)

    def test_property_cache_polymorphic_transitions(self) -> None:
        script = """
        class BoxA {
          init(value) {
            this.value = value;
          }
        }

        class BoxB {
          init(value) {
            this.value = value;
          }
        }

        var a = BoxA(1);
        var b = BoxB(2);
        var total = 0;
        for (var i = 0; i < 20; i = i + 1) {
          if (i < 10) {
            total = total + a.value;
          } else {
            total = total + b.value;
          }
        }
        print total;
        """

        result, stats = self._run_with_cache_stats(script)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self._require_enabled(stats)

        self.assertEqual(stats.get("property_get_shape_transitions"), 2)
        self.assertEqual(stats.get("property_get_misses"), 20)
        self.assertEqual(stats.get("property_set_shape_transitions"), 2)
        self.assertEqual(stats.get("property_set_misses"), 2)

    def test_call_cache_monomorphic(self) -> None:
        script = """
        fun identity(x) {
          return x;
        }

        var i = 0;
        while (i < 12) {
          identity(i);
          i = i + 1;
        }
        print i;
        """

        result, stats = self._run_with_cache_stats(script)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self._require_enabled(stats)

        self.assertGreater(stats.get("call_hits", 0), stats.get("call_misses", 0))
        self.assertEqual(stats.get("call_shape_transitions"), 1)

    def test_call_cache_polymorphic(self) -> None:
        script = """
        fun add_one(x) {
          return x + 1;
        }

        fun double(x) {
          return x + x;
        }

        class Wrapper {
          method(x) {
            return x;
          }
        }

        var wrapper = Wrapper();
        var i = 0;
        while (i < 12) {
          var callee;
          if (i < 4) {
            callee = add_one;
          } else if (i < 8) {
            callee = double;
          } else {
            callee = wrapper.method;
          }
          callee(i);
          i = i + 1;
        }
        print i;
        """

        result, stats = self._run_with_cache_stats(script)
        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self._require_enabled(stats)

        self.assertGreaterEqual(stats.get("call_shape_transitions", 0), 3)
        self.assertGreater(stats.get("call_hits", 0), stats.get("call_misses", 0))


if __name__ == "__main__":
    unittest.main()
