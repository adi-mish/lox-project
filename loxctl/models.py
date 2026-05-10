"""Shared data models for the Lox orchestrator."""

from __future__ import annotations

import enum
from dataclasses import dataclass
from pathlib import Path


class FailureKind(enum.Enum):
    TIMEOUT = "TIMEOUT"
    UNSUPPORTED_MODE = "UNSUPPORTED_MODE"
    EXIT_CODE = "EXIT_CODE"
    STDOUT = "STDOUT"
    STDERR = "STDERR"


@dataclass(frozen=True)
class Command:
    argv: tuple[str | Path, ...]
    cwd: Path

    def display(self) -> str:
        return " ".join(str(part) for part in self.argv)


@dataclass(frozen=True)
class Implementation:
    name: str
    description: str
    build_steps: tuple[Command, ...]
    executable_candidates: tuple[Path, ...]
    clean_paths: tuple[Path, ...]
    supports_scan: bool = False
    supports_print_ast: bool = False
    checks_stderr_fragments: bool = True
    expectation_marker: str | None = None
    default_skip_patterns: tuple[str, ...] = ()

    @property
    def capabilities(self) -> tuple[str, ...]:
        values: list[str] = []
        if self.supports_scan:
            values.append("scan")
        if self.supports_print_ast:
            values.append("print-ast")
        return tuple(values)


@dataclass(frozen=True)
class Expectations:
    stdout: str
    stderr_fragments: tuple[str, ...]
    exit_code: int
    check_stdout: bool


@dataclass
class TestResult:
    path: Path
    expectations: Expectations
    stdout: str = ""
    stderr: str = ""
    exit_code: int = 0
    passed: bool = False
    skipped: bool = False
    duration_seconds: float = 0.0
    skip_reason: str | None = None
    failure_kind: FailureKind | None = None


@dataclass(frozen=True)
class SuiteReport:
    implementation: Implementation
    results: tuple[TestResult, ...]
    failures: tuple[TestResult, ...]
    skipped: tuple[TestResult, ...]

    @property
    def passed_count(self) -> int:
        return len(self.results) - len(self.failures) - len(self.skipped)

    @property
    def failed(self) -> bool:
        return bool(self.failures)

    @property
    def duration_seconds(self) -> float:
        return sum(result.duration_seconds for result in self.results)
