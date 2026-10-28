"""Label vectors for the GlazeWM workspaces widget.

The real Python widget is built offscreen. GlazeWM is never contacted: the client's QWebSocket is
replaced by a fake that reports "connecting" on open and records every text message (the commands),
the monitor lookup returns a fixed handle, and steps feed the raw `query monitors` reply (as the
client would receive it) or a connection status. Window icons are stubbed: every window has a 1x1
icon except handle NO_ICON_HWND (None), and the process id of a window is `hwnd // 2`, so the icon
labels (`icon icon-N`), `hide_label`, `max_icons`, `hide_floating` and `hide_duplicates` show up in
the snapshots. The pixmaps themselves are not compared (the C++ tests check them).
"""

import copy
import json
from typing import Any

from label_registry import LabelExporter, Scenario, register

MONITOR = 65537
OTHER = 65539

NO_ICON_HWND = 702

_commands: list[str] = []


# --- fake GlazeWM `query monitors` data --------------------------------------


def win(hwnd: int, floating: bool = False) -> dict[str, Any]:
    return {
        "type": "window",
        "id": f"w{hwnd}",
        "title": f"win {hwnd}",
        "handle": hwnd,
        "className": "App",
        "processName": "app",
        "displayState": "shown",
        "state": {"type": "floating" if floating else "tiling"},
    }


def ws(
    name: str,
    *,
    display: str | None = None,
    windows: int = 0,
    floating: int = 0,
    focus: bool = False,
    displayed: bool = False,
    split: int = 0,
) -> dict[str, Any]:
    """A workspace; `split` windows sit inside one split child (one child, `split` windows)."""
    children: list[dict[str, Any]] = [win(100 + i) for i in range(windows)]
    children.extend(win(500 + i, floating=True) for i in range(floating))
    if split:
        children.append({"type": "split", "children": [win(700 + i) for i in range(split)]})
    result: dict[str, Any] = {
        "type": "workspace",
        "name": name,
        "hasFocus": focus,
        "isDisplayed": displayed,
        "children": children,
    }
    if display is not None:
        result["displayName"] = display
    return result


def mon(handle: int, workspaces: list[dict[str, Any]]) -> dict[str, Any]:
    return {"type": "monitor", "hardwareId": f"MON{handle}", "handle": handle, "children": workspaces}


def monitors(*mons: dict[str, Any]) -> dict[str, Any]:
    return {"monitors": list(mons)}


def here(*workspaces: dict[str, Any]) -> dict[str, Any]:
    """Only the bar's monitor."""
    return monitors(mon(MONITOR, list(workspaces)))


def conn(connected: bool) -> dict[str, Any]:
    return {"connected": connected}


def click(name: str) -> dict[str, Any]:
    return {"click": name}


def wheel(delta: int) -> dict[str, Any]:
    return {"wheel": delta}


# --- setup -------------------------------------------------------------------


class _FakeSocket:
    def __init__(self, client: Any) -> None:
        self._client = client

    def state(self) -> Any:
        from PyQt6.QtNetwork import QAbstractSocket

        return QAbstractSocket.SocketState.UnconnectedState

    def open(self, _uri: Any) -> None:
        from PyQt6.QtNetwork import QAbstractSocket

        self._client._on_state_changed(QAbstractSocket.SocketState.ConnectingState)

    def sendTextMessage(self, message: str) -> None:  # noqa: N802
        _commands.append(message)


def _setup() -> Any:
    import core.widgets.glazewm.workspaces as module

    from PyQt6.QtGui import QPixmap

    original = module.get_widget_monitor_hwnd
    original_process_info = module.get_process_info
    service = module.IconService.instance()
    module.get_widget_monitor_hwnd = lambda _widget: MONITOR
    module.get_process_info = lambda hwnd: {"pid": hwnd // 2}
    service.get_icon = lambda icon, _size, _dpi: None if icon.hwnd == NO_ICON_HWND else QPixmap(1, 1)
    _commands.clear()

    def restore() -> None:
        module.get_widget_monitor_hwnd = original
        module.get_process_info = original_process_info
        del service.get_icon
        _commands.clear()

    return restore


def _take_commands() -> list[str]:
    taken = list(_commands)
    _commands.clear()
    return taken


def _build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.glazewm.workspaces import GlazewmWorkspacesWidget

    widget = GlazewmWorkspacesWidget(config)
    widget.glazewm_client._websocket = _FakeSocket(widget.glazewm_client)
    return widget


class _Wheel:
    def __init__(self, delta: int) -> None:
        self._delta = delta

    def angleDelta(self) -> Any:  # noqa: N802
        delta = self._delta

        class Point:
            def y(self) -> int:
                return delta

        return Point()


class _LeftPress:
    def button(self) -> Any:
        from PyQt6.QtCore import Qt

        return Qt.MouseButton.LeftButton


def _step(widget: Any, step: dict[str, Any]) -> None:
    client = widget.glazewm_client
    if "connected" in step:
        client.glazewm_connection_status.emit(step["connected"])
    elif "monitors" in step:
        message = {
            "messageType": "client_response",
            "clientMessage": "query monitors",
            "data": {"monitors": copy.deepcopy(step["monitors"])},
        }
        client._handle_message(json.dumps(message))
    elif "click" in step:
        from PyQt6.QtWidgets import QPushButton

        button = widget.workspaces[step["click"]]
        if isinstance(button, QPushButton):
            button.click()
        else:
            button.mousePressEvent(_LeftPress())
    elif "wheel" in step:
        widget.wheelEvent(_Wheel(step["wheel"]))
    else:
        raise ValueError(f"unknown step {step}")


def _label_state(label: Any) -> dict[str, Any]:
    return {"text": label.text(), "class": label.property("class"), "hidden": label.isHidden()}


def _snapshot(widget: Any) -> dict[str, Any]:
    from PyQt6.QtWidgets import QPushButton

    layout = widget.workspace_container_layout
    buttons: list[dict[str, Any]] = []
    for index in range(layout.count()):
        button = layout.itemAt(index).widget()
        entry: dict[str, Any] = {
            "name": button.workspace_name,
            "class": button.property("class"),
            "hidden": button.isHidden(),
        }
        if isinstance(button, QPushButton):
            entry.update(kind="button", text=button.text())
        else:
            entry.update(
                kind="icons",
                text=button.text_label.text(),
                label_hidden=button.text_label.isHidden(),
                icon_classes=[label.property("class") for label in button.icon_labels],
            )
        buttons.append(entry)
    return {
        "widget_hidden": widget.isHidden(),
        "frame_class": widget._widget_frame.property("class"),
        "container_class": widget.workspace_container.property("class"),
        "container_hidden": widget.workspace_container.isHidden(),
        "offline": _label_state(widget.offline_text),
        "button_count": len(widget.workspaces),
        "buttons": buttons,
    }


# --- scenarios ---------------------------------------------------------------

ICONS_POPULATED = {"app_icons": {"enabled_populated": True}}

_LABELS = {
    "populated_label": "P:{name}",
    "empty_label": "E:{display_name}",
    "active_populated_label": "AP[{name}|{display_name}]",
    "active_empty_label": "AE[{name}]",
    "focused_populated_label": "FP<{name}>",
    "focused_empty_label": "FE<{display_name}>",
}


def _status_cycle() -> list[dict[str, Any]]:
    """Two monitors; the focus and displayed workspaces move so every status shows up."""
    other = mon(OTHER, [ws("4", display="Four", windows=1, displayed=True), ws("5")])
    return [
        monitors(
            mon(MONITOR, [ws("1", display="One", windows=2, focus=True, displayed=True), ws("2", windows=1), ws("3")]),
            other,
        ),
        monitors(
            mon(MONITOR, [ws("1", display="One", windows=2, displayed=True), ws("2", windows=1), ws("3")]),
            mon(OTHER, [ws("4", display="Four", windows=1, displayed=True, focus=True), ws("5")]),
        ),
        monitors(
            mon(MONITOR, [ws("1", display="One", windows=2), ws("2", windows=1, displayed=True), ws("3", focus=True)]),
            other,
        ),
        monitors(
            mon(MONITOR, [ws("1", display="One", windows=2), ws("2", windows=1), ws("3", displayed=True)]),
            mon(OTHER, [ws("4", display="Four", windows=1, displayed=True), ws("5", focus=True)]),
        ),
        monitors(
            mon(MONITOR, [ws("1", display="One", windows=2), ws("2", windows=1, focus=True, displayed=True), ws("3")]),
            other,
        ),
    ]


def _status_steps() -> list[dict[str, Any]]:
    return [conn(True), *_status_cycle()]


register(
    LabelExporter(
        widget="glazewm_workspaces",
        widget_type="glazewm.workspaces.GlazewmWorkspacesWidget",
        build=_build,
        apply_step=_step,
        setup=_setup,
        snapshot=_snapshot,
        include_normalized=True,
        step_commands=_take_commands,
        scenarios=[
            Scenario(
                "offline_online_defaults",
                steps=[
                    monitors(mon(MONITOR, [ws("1", windows=1, focus=True, displayed=True)])),
                    conn(True),
                    here(ws("1", windows=1, focus=True, displayed=True), ws("2")),
                    conn(False),
                    conn(True),
                ],
            ),
            Scenario(
                "hide_if_offline",
                {"hide_if_offline": True, "offline_label": "no glaze"},
                steps=[conn(True), here(ws("1", focus=True, displayed=True)), conn(False), conn(True)],
            ),
            Scenario("offline_label_shown", {"offline_label": "GlazeWM down"}, steps=[conn(False), conn(True)]),
            Scenario("status_matrix_exclusive", steps=_status_steps()),
            Scenario("status_matrix_not_exclusive", {"monitor_exclusive": False}, steps=_status_steps()),
            Scenario("labels_exclusive", dict(_LABELS), steps=_status_steps()),
            Scenario("labels_not_exclusive", {**_LABELS, "monitor_exclusive": False}, steps=_status_steps()),
            Scenario(
                "labels_focused_fall_back_to_active",
                {"active_populated_label": "AP-{name}", "active_empty_label": "AE-{display_name}"},
                steps=_status_steps(),
            ),
            Scenario(
                "labels_only_focused",
                {"focused_populated_label": "[{name}]", "focused_empty_label": "<{name}>"},
                steps=_status_steps(),
            ),
            Scenario(
                "labels_display_name_and_name_fallbacks",
                steps=[
                    conn(True),
                    here(
                        ws("1", display="Code", windows=1, focus=True, displayed=True),
                        ws("2", display="", windows=1),
                        ws("3", display="Chat"),
                        ws("4"),
                    ),
                ],
            ),
            Scenario(
                "labels_empty_config_strings_fall_back",
                {"populated_label": "", "empty_label": "", "active_populated_label": "", "focused_empty_label": ""},
                steps=[
                    conn(True),
                    here(ws("1", display="Code", windows=1, displayed=True), ws("2", display="Web"), ws("3", focus=True)),
                ],
            ),
            Scenario(
                "labels_literal_braces",
                {"populated_label": "{{{name}}}", "empty_label": "{display_name!s}-{name:>3}"},
                steps=[conn(True), here(ws("a", display="x", windows=1), ws("b", display="y"))],
            ),
            Scenario(
                "natural_sort_order",
                {"monitor_exclusive": False},
                steps=[
                    conn(True),
                    here(ws("10"), ws("2"), ws("1")),
                    here(ws("10"), ws("2"), ws("1"), ws("b"), ws("A"), ws("a1"), ws("a10"), ws("a2")),
                    here(ws("3"), ws("10"), ws("2"), ws("1"), ws("b"), ws("A"), ws("a1"), ws("a10"), ws("a2"), ws("01")),
                    here(ws("1-2"), ws("1-10"), ws("1-1"), ws("web2"), ws("Web10"), ws("WEB1")),
                ],
            ),
            Scenario(
                "workspaces_disappear_and_return_exclusive",
                steps=[
                    conn(True),
                    here(ws("1", windows=1, focus=True, displayed=True), ws("2", windows=2), ws("3")),
                    here(ws("1", windows=1, focus=True, displayed=True)),
                    here(ws("1", windows=1, displayed=True), ws("3", windows=1, focus=True)),
                    here(ws("1", windows=1, focus=True, displayed=True), ws("2", windows=2), ws("3")),
                ],
            ),
            Scenario(
                "workspaces_disappear_and_return_not_exclusive",
                {"monitor_exclusive": False},
                steps=[
                    conn(True),
                    here(ws("1", windows=1, focus=True), ws("2", windows=2), ws("3")),
                    here(ws("1", windows=1, focus=True)),
                    monitors(mon(MONITOR, [ws("1", windows=1)]), mon(OTHER, [ws("2", windows=2, focus=True)])),
                    here(ws("1", windows=1, focus=True), ws("2", windows=2), ws("3")),
                ],
            ),
            Scenario(
                "workspace_moves_between_monitors",
                steps=[
                    conn(True),
                    monitors(
                        mon(MONITOR, [ws("1", windows=1, focus=True, displayed=True), ws("2", windows=1)]),
                        mon(OTHER, [ws("3", displayed=True)]),
                    ),
                    monitors(
                        mon(MONITOR, [ws("1", windows=1, focus=True, displayed=True)]),
                        mon(OTHER, [ws("2", windows=1, displayed=True), ws("3")]),
                    ),
                ],
            ),
            Scenario(
                "workspace_moves_between_monitors_not_exclusive",
                {"monitor_exclusive": False},
                steps=[
                    conn(True),
                    monitors(
                        mon(MONITOR, [ws("1", windows=1, focus=True, displayed=True), ws("2", windows=1)]),
                        mon(OTHER, [ws("3", displayed=True)]),
                    ),
                    monitors(
                        mon(MONITOR, [ws("1", windows=1, displayed=True)]),
                        mon(OTHER, [ws("2", windows=1, displayed=True), ws("3", focus=True)]),
                    ),
                ],
            ),
            Scenario(
                "other_monitor_only_ignored",
                steps=[
                    conn(True),
                    monitors(mon(OTHER, [ws("9", windows=1, focus=True, displayed=True)])),
                    here(ws("1", windows=1, focus=True, displayed=True)),
                    monitors(mon(OTHER, [ws("9", windows=1, focus=True, displayed=True)])),
                    {"monitors": []},
                ],
            ),
            Scenario(
                "focus_flags_last_workspace_wins",
                steps=[
                    conn(True),
                    monitors(
                        mon(MONITOR, [ws("1", windows=1, focus=True, displayed=True), ws("2", windows=1, focus=True)]),
                        mon(OTHER, [ws("3", focus=True)]),
                    ),
                    monitors(mon(MONITOR, [ws("1", windows=1, displayed=True), ws("2", windows=1)])),
                ],
            ),
            Scenario(
                "duplicate_workspace_names",
                steps=[
                    conn(True),
                    monitors(
                        mon(MONITOR, [ws("1", windows=1, displayed=True), ws("1", display="dup", windows=2)]),
                        mon(OTHER, [ws("1", windows=3, focus=True)]),
                    ),
                ],
            ),
            Scenario(
                "window_counts_are_children",
                steps=[
                    conn(True),
                    here(
                        ws("1", windows=1, floating=1, displayed=True),
                        ws("2", split=3),
                        ws("3", floating=1),
                    ),
                ],
            ),
            Scenario(
                "icon_buttons_populated",
                dict(ICONS_POPULATED),
                steps=_status_steps(),
            ),
            Scenario(
                "icon_buttons_labels_and_hide_label",
                {
                    **_LABELS,
                    "app_icons": {"enabled_active": True, "enabled_focused": True, "hide_label": True, "size": 20},
                },
                steps=_status_steps(),
            ),
            Scenario(
                "icon_buttons_only_focused_enabled",
                {"app_icons": {"enabled_focused": True}, "monitor_exclusive": False},
                steps=_status_steps(),
            ),
            Scenario(
                "plain_buttons_when_focused_icons_disabled",
                {"app_icons": {"enabled_focused": False, "hide_duplicates": True, "max_icons": 2}},
                steps=[conn(True), here(ws("1", windows=1, focus=True, displayed=True), ws("2", windows=2))],
            ),
            Scenario(
                "icon_buttons_floating_and_many_windows",
                {"app_icons": {"enabled_populated": True, "enabled_active": True, "hide_floating": True, "max_icons": 1}},
                steps=[
                    conn(True),
                    here(
                        ws("1", windows=3, floating=2, focus=True, displayed=True),
                        ws("2", floating=1),
                        ws("3", split=2),
                    ),
                ],
            ),
            Scenario(
                "icon_buttons_duplicates_and_missing_icon",
                {"app_icons": {"enabled_populated": True, "enabled_active": True, "hide_duplicates": True}},
                steps=[
                    conn(True),
                    here(ws("1", windows=4, focus=True, displayed=True), ws("2", split=3), ws("3", windows=2)),
                ],
            ),
            Scenario(
                "clicks_activate_by_name",
                steps=[
                    conn(True),
                    here(ws("1", windows=1, focus=True, displayed=True), ws("web", display="Web"), ws("10")),
                    click("1"),
                    click("web"),
                    click("10"),
                ],
            ),
            Scenario(
                "clicks_on_icon_buttons",
                dict(ICONS_POPULATED),
                steps=[conn(True), here(ws("1", windows=1, focus=True, displayed=True), ws("two")), click("two"), click("1")],
            ),
            Scenario(
                "wheel_exclusive",
                steps=[
                    conn(True),
                    here(ws("1", focus=True, displayed=True), ws("2")),
                    wheel(120),
                    wheel(-120),
                    wheel(0),
                    wheel(240),
                ],
            ),
            Scenario(
                "wheel_global_when_not_exclusive",
                {"monitor_exclusive": False},
                steps=[conn(True), here(ws("1", focus=True), ws("2")), wheel(120), wheel(-120), wheel(0)],
            ),
            Scenario(
                "wheel_reversed",
                {"reverse_scroll_direction": True},
                steps=[conn(True), here(ws("1", focus=True, displayed=True)), wheel(120), wheel(-120)],
            ),
            Scenario(
                "wheel_reversed_not_exclusive",
                {"reverse_scroll_direction": True, "monitor_exclusive": False},
                steps=[conn(True), here(ws("1", focus=True)), wheel(120), wheel(-120)],
            ),
            Scenario(
                "wheel_disabled",
                {"enable_scroll_switching": False},
                steps=[conn(True), here(ws("1", focus=True, displayed=True)), wheel(120), wheel(-120)],
            ),
        ],
    )
)
