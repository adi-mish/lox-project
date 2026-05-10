"""Process and artifact helpers."""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

from .models import Command, Implementation


def run_checked(command: Command) -> None:
    print(f"$ {command.display()}", flush=True)
    completed = subprocess.run(
        [str(part) for part in command.argv],
        cwd=command.cwd,
    )
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def build_impl(impl: Implementation, *, stats: bool = False) -> None:
    if stats and not impl.supports_stats:
        raise SystemExit(f"{impl.name} does not support stats builds")
    steps = impl.stats_build_steps if stats else impl.build_steps
    for step in steps:
        run_checked(step)


def resolve_executable(impl: Implementation, *, stats: bool = False) -> Path:
    if stats and not impl.supports_stats:
        raise SystemExit(f"{impl.name} does not support --stats")
    candidates_to_try = (
        impl.stats_executable_candidates if stats else impl.executable_candidates
    )
    for candidate in candidates_to_try:
        if candidate.exists() and os.access(candidate, os.X_OK):
            return candidate
    candidates = ", ".join(str(path) for path in candidates_to_try)
    raise SystemExit(f"No executable found for {impl.name}. Tried: {candidates}")


def remove_path(path: Path, dry_run: bool = False) -> bool:
    if not path.exists():
        return False
    if dry_run:
        print(f"would remove {path}")
        return True
    if path.is_dir():
        shutil.rmtree(path)
    else:
        path.unlink()
    print(f"removed {path}")
    return True


def clean_impl(impl: Implementation, dry_run: bool = False) -> int:
    removed = 0
    for path in impl.clean_paths:
        if remove_path(path, dry_run=dry_run):
            removed += 1
    if removed == 0:
        print(f"{impl.name}: nothing to clean")
    return removed


def which(program: str) -> str | None:
    return shutil.which(program)
