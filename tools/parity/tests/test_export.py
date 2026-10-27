"""Checks for the vector exporter. Run with the YASB virtualenv interpreter:

    D:\\yasb\\.venv\\Scripts\\python.exe -m pytest tools\\parity\\tests   (if pytest is available)
    D:\\yasb\\.venv\\Scripts\\python.exe tools\\parity\\tests\\test_export.py   (plain runner)

YASB_ROOT overrides the default checkout location.
"""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

PARITY = Path(__file__).resolve().parents[1]
YASB_ROOT = Path(os.environ.get("YASB_ROOT", r"D:\yasb"))
EXPECTED_WIDGETS = {
    "yasb.control_center.ControlCenterWidget",
    "komorebi.workspaces.WorkspaceWidget",
    "komorebi.active_layout.ActiveLayoutWidget",
    "yasb.taskbar.TaskbarWidget",
    "yasb.clock.ClockWidget",
    "yasb.media.MediaWidget",
    "yasb.systray.SystrayWidget",
    "yasb.wallpapers.WallpapersWidget",
    "yasb.github.GithubWidget",
    "yasb.notifications.NotificationsWidget",
    "yasb.cpu.CpuWidget",
    "yasb.memory.MemoryWidget",
    "yasb.bluetooth.BluetoothWidget",
    "yasb.wifi.WifiWidget",
    "yasb.battery.BatteryWidget",
}


def run_export(out: Path) -> None:
    subprocess.run(
        [sys.executable, str(PARITY / "export_vectors.py"), "--yasb", str(YASB_ROOT), "--out", str(out)],
        check=True,
    )


def snapshot(directory: Path) -> dict[str, bytes]:
    return {p.relative_to(directory).as_posix(): p.read_bytes() for p in sorted(directory.rglob("*")) if p.is_file()}


def test_exports_are_byte_identical_and_lf() -> None:
    with tempfile.TemporaryDirectory(dir=PARITY / ".sandbox" if (PARITY / ".sandbox").exists() else None) as base:
        first, second = Path(base, "a"), Path(base, "b")
        run_export(first)
        run_export(second)
        one, two = snapshot(first), snapshot(second)
    assert one.keys() == two.keys()
    assert [name for name in one if one[name] != two[name]] == []
    assert all(b"\r" not in data for name, data in one.items() if name.endswith(".json"))


def test_user_config_vector_covers_all_widgets_and_unknown_option_is_rejected() -> None:
    config = json.loads((PARITY.parents[1] / "tests" / "vectors" / "config.json").read_text(encoding="utf-8"))
    widgets = config["user"]["widgets"]
    assert {entry["type"] for entry in widgets.values()} == EXPECTED_WIDGETS
    assert all(entry["status"] == "ok" for entry in widgets.values())
    rejected = config["unknown_option"]["widgets"]["cpu"]
    assert rejected["status"] == "invalid_options"
    assert rejected["errors"][0]["type"] == "extra_forbidden"


if __name__ == "__main__":
    test_exports_are_byte_identical_and_lf()
    test_user_config_vector_covers_all_widgets_and_unknown_option_is_rejected()
    print("ok")
