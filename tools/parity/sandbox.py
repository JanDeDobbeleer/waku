"""Isolated environment for importing and running YASB, modelled on tests/conftest.py."""

import os
import shutil
import sys
import tempfile
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

REDIRECTED_ENV = ("YASB_CONFIG_HOME", "LOCALAPPDATA")


@contextmanager
def yasb_sandbox(yasb_root: Path, base_dir: Path) -> Iterator[Path]:
    """Redirect YASB state into a scratch folder, import path set up, Qt offscreen.

    Must be entered before any `core.*` or `settings` import: those modules read the
    environment at import time. Yields the sandbox config directory.
    """
    base_dir.mkdir(parents=True, exist_ok=True)
    sandbox = Path(tempfile.mkdtemp(prefix="sandbox-", dir=base_dir))
    saved = {name: os.environ.get(name) for name in (*REDIRECTED_ENV, "QT_QPA_PLATFORM")}
    path_before = list(sys.path)
    try:
        for name in REDIRECTED_ENV:
            target = sandbox / name.lower()
            target.mkdir()
            os.environ[name] = str(target)
        os.environ["QT_QPA_PLATFORM"] = "offscreen"
        sys.path.insert(0, str(yasb_root / "src"))
        yield Path(os.environ["YASB_CONFIG_HOME"])
    finally:
        sys.path[:] = path_before
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
        shutil.rmtree(sandbox, ignore_errors=True)
