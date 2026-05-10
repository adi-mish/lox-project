"""Repository paths and common runtime constants."""

from __future__ import annotations

import os
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TEST_DIR = REPO_ROOT / "test"
DEFAULT_TIMEOUT_SECONDS = 10
BENCHMARK_TIMEOUT_SECONDS = 60


def repo_path(*parts: str) -> Path:
    return REPO_ROOT.joinpath(*parts)


def max_parallel_jobs() -> str:
    return str(max(1, min(os.cpu_count() or 2, 8)))
