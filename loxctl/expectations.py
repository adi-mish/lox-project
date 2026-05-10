"""Official Crafting Interpreters test expectation parsing."""

from __future__ import annotations

import re
from pathlib import Path

from .models import Expectations, Implementation


def normalize_output(stream: str | bytes | None) -> str:
    if stream is None:
        return ""
    if isinstance(stream, bytes):
        return stream.decode("utf-8", errors="replace").rstrip()
    return stream.rstrip()


def _strip_nested_comment_markers(comment: str) -> str:
    text = comment.strip()
    while text.startswith("//"):
        text = text[2:].strip()
    return text


def parse_expectations(path: Path, impl: Implementation) -> Expectations:
    expected_stdout: list[str] = []
    stderr_fragments: list[str] = []
    saw_parse_error = False
    saw_runtime_error = False
    saw_expect_line = False

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        comment_start = raw_line.find("//")
        if comment_start == -1:
            continue

        comment = raw_line[comment_start:].strip()
        text = _strip_nested_comment_markers(comment)

        expect_match = re.match(r"^expect:\s*(.*)$", text, re.IGNORECASE)
        if expect_match:
            expected_stdout.append(expect_match.group(1).strip())
            saw_expect_line = True
            continue

        runtime_match = re.match(
            r"^expect runtime error:\s*(.*)$", text, re.IGNORECASE
        )
        if runtime_match:
            saw_runtime_error = True
            fragment = runtime_match.group(1).strip()
            if fragment:
                stderr_fragments.append(fragment)
            continue

        marker_match = re.match(r"^\[(java|c) line (\d+)\](.*)$", text)
        if marker_match:
            if marker_match.group(1) == impl.expectation_marker:
                saw_parse_error = True
                stderr_fragments.append(
                    f"[line {marker_match.group(2)}]{marker_match.group(3)}"
                )
            continue

        if text.startswith("[line") and "Error" in text:
            saw_parse_error = True
            stderr_fragments.append(text)
        elif text.startswith("Error") or "Error at" in text:
            saw_parse_error = True
            stderr_fragments.append(text)

    if saw_parse_error:
        exit_code = 65
    elif saw_runtime_error:
        exit_code = 70
    else:
        exit_code = 0

    return Expectations(
        stdout="\n".join(expected_stdout),
        stderr_fragments=tuple(stderr_fragments),
        exit_code=exit_code,
        check_stdout=saw_expect_line,
    )
