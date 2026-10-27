"""Shared helpers for the vector exporters: deterministic JSON, secret scrubbing, log capture."""

import dataclasses
import json
import logging
import re
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path
from typing import Any

SECRET_PLACEHOLDER = "<REDACTED>"

_SECRET_KEY = re.compile(
    r"^(?P<head>\s*-?\s*[\w.-]*(?:token|secret|passw(?:or)?d|api[_-]?key|authorization|credential)[\w.-]*\s*:\s*)"
    r"(?P<value>(?!<REDACTED>)\S.*?)(?P<tail>\s*(?:#.*)?)$",
    re.IGNORECASE,
)
_SECRET_VALUE = re.compile(
    r"\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-[A-Za-z0-9_-]{20,}"
    r"|xox[abprs]-[A-Za-z0-9-]{10,}|eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,})"
)


def jsonable(value: Any) -> Any:
    """Plain JSON types only; sets are sorted so output never depends on hash order."""
    if dataclasses.is_dataclass(value) and not isinstance(value, type):
        return jsonable(dataclasses.asdict(value))
    if isinstance(value, dict):
        return {str(key): jsonable(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [jsonable(item) for item in value]
    if isinstance(value, (set, frozenset)):
        return sorted((jsonable(item) for item in value), key=lambda item: json.dumps(item, sort_keys=True))
    if isinstance(value, Path):
        return value.as_posix()
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    return str(value)


def dumps(value: Any) -> str:
    return json.dumps(jsonable(value), sort_keys=True, indent=2, ensure_ascii=True) + "\n"


def write_text(path: Path, text: str) -> None:
    """UTF-8, no BOM, LF endings, regardless of platform."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def write_json(path: Path, value: Any) -> None:
    write_text(path, dumps(value))


def read_text(path: Path) -> str:
    """Read UTF-8 text, normalising CRLF so inputs and vectors are byte-stable."""
    return path.read_bytes().decode("utf-8-sig").replace("\r\n", "\n")


def scrub_secrets(text: str, *, scan_keys: bool = True) -> tuple[str, int]:
    """Replace token-looking values with a placeholder; returns the text and the hit count."""
    hits = 0
    lines: list[str] = []
    for line in text.split("\n"):
        match = _SECRET_KEY.match(line) if scan_keys else None
        if match and match.group("value").strip("'\"") not in ("", "null", "~"):
            line = f"{match.group('head')}\"{SECRET_PLACEHOLDER}\"{match.group('tail')}"
            hits += 1
        replaced, count = _SECRET_VALUE.subn(SECRET_PLACEHOLDER, line)
        hits += count
        lines.append(replaced)
    return "\n".join(lines), hits


class LogCapture(logging.Handler):
    def __init__(self) -> None:
        super().__init__(logging.DEBUG)
        self.records: list[dict[str, str]] = []

    def emit(self, record: logging.LogRecord) -> None:
        self.records.append({"level": record.levelname, "logger": record.name, "message": record.getMessage()})


@contextmanager
def capture_logs() -> Iterator[LogCapture]:
    handler = LogCapture()
    root = logging.getLogger()
    previous = root.level
    root.addHandler(handler)
    root.setLevel(logging.DEBUG)
    try:
        yield handler
    finally:
        root.removeHandler(handler)
        root.setLevel(previous)


def list_fixture_files(directory: Path, suffix: str) -> list[Path]:
    return sorted(directory.glob(f"*{suffix}"), key=lambda path: path.name)
