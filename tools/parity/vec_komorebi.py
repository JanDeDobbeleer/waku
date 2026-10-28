"""Label vectors for the komorebi widgets (workspaces and active layout).

The real Python widgets are built offscreen. Komorebi itself is never contacted: steps feed
recorded-style JSON state/event dictionaries straight into the widget slots, the monitor lookup
returns a fixed id, and `subprocess.Popen` is replaced by a recorder, so each step also records the
`komorebic` commands the widget would have started.
"""

import copy
from typing import Any

from label_registry import LabelExporter, Scenario, register

MONITOR_ID = 65537
OTHER_MONITOR_ID = 65539

_commands: list[Any] = []


# --- fake komorebi state -----------------------------------------------------


def window(hwnd: int) -> dict[str, Any]:
    return {"hwnd": hwnd, "title": "", "exe": "app.exe", "class": "App"}


def container(*hwnds: int) -> dict[str, Any]:
    return {"id": f"c{hwnds[0]}", "windows": {"elements": [window(h) for h in hwnds], "focused": 0}}


def workspace(
    name: str | None = None,
    *,
    windows: int = 0,
    floating: int = 0,
    tile: bool = True,
    layout: Any = "BSP",
    monocle: bool = False,
    maximized: bool = False,
    layer: str | None = "Tiling",
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "name": name,
        "containers": {"elements": [container(100 + i) for i in range(windows)], "focused": 0},
        "monocle_container": container(900) if monocle else None,
        "maximized_window": window(901) if maximized else None,
        "floating_windows": {"elements": [window(500 + i) for i in range(floating)], "focused": 0},
        "layout": {"Default": layout} if isinstance(layout, str) else layout,
        "tile": tile,
    }
    if layer is not None:
        result["layer"] = layer
    return result


def monitor(monitor_id: int, workspaces: list[dict[str, Any]], focused: int = 0) -> dict[str, Any]:
    return {
        "id": monitor_id,
        "name": f"DISPLAY{monitor_id}",
        "workspaces": {"elements": workspaces, "focused": focused},
    }


def state(
    workspaces: list[dict[str, Any]],
    focused: int = 0,
    *,
    paused: bool = False,
    float_override: bool = False,
    other_monitor: bool = False,
) -> dict[str, Any]:
    monitors = [monitor(MONITOR_ID, workspaces, focused)]
    if other_monitor:
        monitors.append(monitor(OTHER_MONITOR_ID, [workspace("X")], 0))
    return {
        "monitors": {"elements": monitors, "focused": 0},
        "is_paused": paused,
        "float_override": float_override,
    }


def foreign_state() -> dict[str, Any]:
    """A state that has no monitor for this bar."""
    return {
        "monitors": {"elements": [monitor(OTHER_MONITOR_ID, [workspace("A")], 0)], "focused": 0},
        "is_paused": False,
    }


def event(kind: str, content: Any = None) -> dict[str, Any]:
    return {"type": kind, "content": content}


def connect(st: dict[str, Any]) -> dict[str, Any]:
    return {"connect": st}


def update(ev: dict[str, Any], st: dict[str, Any]) -> dict[str, Any]:
    return {"update": {"event": ev, "state": st}}


DISCONNECT = {"disconnect": True}


# --- shared setup ------------------------------------------------------------


def _normalize_commands(recorded: list[Any]) -> list[list[str]]:
    """Each Popen call becomes a list of komorebic argument lists (shell chains are split)."""
    result: list[list[str]] = []
    for call in recorded:
        if isinstance(call, str):
            for part in call.split(" && "):
                result.append(part.replace('"komorebic.exe" ', "", 1).split())
        else:
            result.append([str(arg) for arg in call[1:]])
    return result


def _setup(module: str) -> Any:
    from importlib import import_module

    def setup() -> Any:
        import subprocess

        widget_module = import_module(f"core.widgets.komorebi.{module}")
        original_hwnd = widget_module.get_widget_monitor_hwnd
        original_popen = subprocess.Popen
        widget_module.get_widget_monitor_hwnd = lambda _widget: MONITOR_ID

        class Recorder:
            def __init__(self, args: Any, *_args: Any, **_kwargs: Any) -> None:
                _commands.append(args)

        subprocess.Popen = Recorder  # type: ignore[misc,assignment]
        _commands.clear()

        def restore() -> None:
            widget_module.get_widget_monitor_hwnd = original_hwnd
            subprocess.Popen = original_popen  # type: ignore[misc]
            _commands.clear()

        return restore

    return setup


def _take_commands() -> list[list[str]]:
    taken = _normalize_commands(list(_commands))
    _commands.clear()
    return taken


class _Wheel:
    def __init__(self, delta: int) -> None:
        self._delta = delta

    def angleDelta(self) -> Any:  # noqa: N802
        delta = self._delta

        class Point:
            def y(self) -> int:
                return delta

        return Point()


def _label_state(label: Any) -> dict[str, Any]:
    return {"text": label.text(), "class": label.property("class"), "hidden": label.isHidden()}


def _teardown(widget: Any) -> None:
    widget._on_destroyed()
    _commands.clear()


# --- workspaces --------------------------------------------------------------


def _workspaces_snapshot(widget: Any) -> dict[str, Any]:
    layer = getattr(widget, "workspace_layer_label", None)
    return {
        "widget_hidden": widget.isHidden(),
        "frame_class": widget._widget_frame.property("class"),
        "container_class": widget.workspace_container.property("class"),
        "container_hidden": widget.workspace_container.isHidden(),
        "offline": _label_state(widget._offline_text),
        "float_override": _label_state(widget.float_override_label),
        "layer": _label_state(layer) if layer is not None else None,
        "buttons": [{"index": button.workspace_index, **_label_state(button)} for button in widget.workspace_buttons],
    }


def _workspaces_build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.komorebi.workspaces import WorkspaceWidget

    return WorkspaceWidget(config)


def _workspaces_step(widget: Any, step: dict[str, Any]) -> None:
    if "connect" in step:
        widget._on_komorebi_connect_event(copy.deepcopy(step["connect"]))
    elif "update" in step:
        data = copy.deepcopy(step["update"])
        widget._on_komorebi_update_event(data["event"], data["state"])
    elif "disconnect" in step:
        widget._on_komorebi_disconnect_event()
    elif "click" in step:
        widget.workspace_buttons[step["click"]].click()
    elif "wheel" in step:
        widget.wheelEvent(_Wheel(step["wheel"]))
    else:
        raise ValueError(f"unknown step {step}")


def _ws3(focused: int = 0, **kwargs: Any) -> dict[str, Any]:
    return state([workspace("I", windows=1), workspace("II"), workspace("III", floating=1)], focused, **kwargs)


_WS_NAMED = [workspace("code", windows=2), workspace("web", windows=1), workspace(None), workspace("chat")]

register(
    LabelExporter(
        widget="komorebi_workspaces",
        widget_type="komorebi.workspaces.WorkspaceWidget",
        build=_workspaces_build,
        apply_step=_workspaces_step,
        setup=_setup("workspaces"),
        teardown_widget=_teardown,
        snapshot=_workspaces_snapshot,
        include_normalized=True,
        step_commands=_take_commands,
        scenarios=[
            Scenario(
                "user_options_focus_and_click",
                steps=[
                    connect(_ws3()),
                    update(event("FocusWorkspaceNumber", 1), _ws3(1)),
                    update(event("FocusWorkspaceNumber", 2), _ws3(2)),
                    {"click": 0},
                    {"click": 2},
                    update(
                        event("Manage"),
                        state([workspace("I", windows=2), workspace("II"), workspace("III", floating=1)], 2),
                    ),
                    DISCONNECT,
                    connect(_ws3(1)),
                ],
            ),
            Scenario(
                "scroll_wraps",
                steps=[
                    connect(_ws3(0)),
                    {"wheel": 120},
                    {"wheel": -120},
                    update(event("FocusWorkspaceNumber", 2), _ws3(2)),
                    {"wheel": -120},
                    {"wheel": 0},
                ],
            ),
            Scenario(
                "scroll_reversed",
                {"reverse_scroll_direction": True},
                steps=[connect(_ws3(1)), {"wheel": 120}, {"wheel": -120}],
            ),
            Scenario(
                "scroll_disabled",
                {"enable_scroll_switching": False},
                steps=[connect(_ws3(1)), {"wheel": 120}],
            ),
            Scenario(
                "hide_empty_workspaces",
                {"hide_empty_workspaces": True},
                steps=[
                    connect(_ws3(0)),
                    update(event("FocusWorkspaceNumber", 1), _ws3(1)),
                    update(event("FocusWorkspaceNumber", 0), _ws3(0)),
                    update(
                        event("Unmanage"),
                        state([workspace("I"), workspace("II", windows=1), workspace("III", floating=1)], 0),
                    ),
                ],
            ),
            Scenario(
                "label_templates_and_names",
                {
                    "label_workspace_btn": "{name}",
                    "label_workspace_active_btn": "[{name}]",
                    "label_workspace_populated_btn": "{index}.{monitor_index}",
                    "label_default_name": "ws{index}",
                },
                steps=[
                    connect(state(_WS_NAMED, 0)),
                    update(event("FocusWorkspaceNumber", 2), state(_WS_NAMED, 2)),
                    update(
                        event("WorkspaceName", [0, 3, "chat2"]),
                        state([workspace("code2", windows=2), *_WS_NAMED[1:]], 2),
                    ),
                ],
            ),
            Scenario(
                "label_zero_index",
                {"label_zero_index": True, "label_workspace_btn": "{index}", "label_workspace_active_btn": "{index}!"},
                steps=[connect(_ws3(1))],
            ),
            Scenario(
                "new_and_closed_workspace",
                steps=[
                    connect(_ws3(0)),
                    update(
                        event("NewWorkspace"),
                        state([workspace("I", windows=1), workspace("II"), workspace("III"), workspace("IV")], 0),
                    ),
                    update(event("CloseWorkspace"), state([workspace("I", windows=1), workspace("II")], 0)),
                    update(event("EnsureWorkspaces"), _ws3(0)),
                ],
            ),
            Scenario(
                "window_moves_empty_workspace",
                steps=[
                    connect(state([workspace("I", windows=1), workspace("II", windows=1)], 0)),
                    update(
                        event("MoveContainerToWorkspaceNumber", 0),
                        state([workspace("I", windows=1), workspace("II")], 0),
                    ),
                    update(event("Show"), state([workspace("I", windows=1), workspace("II", windows=2)], 0)),
                ],
            ),
            Scenario(
                "move_workspace_to_monitor",
                steps=[
                    connect(_ws3(0, other_monitor=True)),
                    update(
                        event("MoveWorkspaceToMonitorNumber", 1),
                        state([workspace("I", windows=1), workspace("II")], 0, other_monitor=True),
                    ),
                    update(event("MoveWorkspaceToMonitorNumber", 0), _ws3(0, other_monitor=True)),
                ],
            ),
            Scenario(
                "float_override_label",
                steps=[
                    connect(_ws3(0)),
                    update(event("ToggleFloatOverride"), _ws3(0, float_override=True)),
                    update(event("ToggleFloatOverride"), _ws3(0)),
                ],
            ),
            Scenario(
                "workspace_layer_label",
                {"toggle_workspace_layer": {"enabled": True}},
                steps=[
                    connect(state([workspace("I", windows=1, layer="Floating"), workspace("II")], 0)),
                    update(
                        event("FocusChange"),
                        state([workspace("I", windows=1, layer="Floating"), workspace("II")], 0),
                    ),
                    update(
                        event("ToggleWorkspaceLayer"),
                        state([workspace("I", windows=1, layer="Tiling"), workspace("II")], 0),
                    ),
                    update(
                        event("FocusChange"),
                        state([workspace("I", windows=1, layer="Tiling"), workspace("II")], 0),
                    ),
                    DISCONNECT,
                ],
            ),
            Scenario(
                "offline_label_and_visible_when_offline",
                {"hide_if_offline": False, "label_offline": "offline"},
                steps=[connect(_ws3(0)), DISCONNECT, connect(_ws3(0))],
            ),
            Scenario(
                "other_monitor_ignored",
                steps=[connect(foreign_state()), update(event("FocusWorkspaceNumber", 0), _ws3(0))],
            ),
        ],
    )
)


# --- active layout -----------------------------------------------------------


def _layout_snapshot(widget: Any) -> dict[str, Any]:
    return {
        "widget_hidden": widget.isHidden(),
        "frame_class": widget._widget_frame.property("class"),
        "container_class": widget._widget_container.property("class"),
        "label": _label_state(widget._active_layout_text),
    }


def _layout_build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.komorebi.active_layout import ActiveLayoutWidget

    return ActiveLayoutWidget(config)


def _layout_step(widget: Any, step: dict[str, Any]) -> None:
    if "connect" in step:
        widget._on_komorebi_connect_event(copy.deepcopy(step["connect"]))
    elif "update" in step:
        data = copy.deepcopy(step["update"])
        widget._on_komorebi_layout_change_event(data["event"], data["state"])
    elif "disconnect" in step:
        widget._on_komorebi_disconnect_event()
    elif "callback" in step:
        widget._run_callback(step["callback"])
    else:
        raise ValueError(f"unknown step {step}")


def _one(paused: bool = False, **kwargs: Any) -> dict[str, Any]:
    return state([workspace("I", **kwargs), workspace("II")], 0, paused=paused)


register(
    LabelExporter(
        widget="komorebi_active_layout",
        widget_type="komorebi.active_layout.ActiveLayoutWidget",
        build=_layout_build,
        apply_step=_layout_step,
        setup=_setup("active_layout"),
        teardown_widget=_teardown,
        snapshot=_layout_snapshot,
        include_normalized=True,
        step_commands=_take_commands,
        scenarios=[
            Scenario(
                "user_options_states_and_cycling",
                steps=[
                    connect(_one()),
                    {"callback": "next_layout"},
                    {"callback": "next_layout"},
                    {"callback": "prev_layout"},
                    update(event("ChangeLayout"), _one(layout="Columns")),
                    update(event("ChangeLayout"), _one(layout="Grid")),
                    update(event("ChangeLayout"), _one(layout="Scrolling")),
                    update(event("ChangeLayout"), _one(layout="VerticalStack")),
                    update(event("ChangeLayout"), _one(layout="HorizontalStack")),
                    update(event("ChangeLayout"), _one(layout="UltrawideVerticalStack")),
                    update(event("ChangeLayout"), _one(layout="RightMainVerticalStack")),
                    update(event("ChangeLayout"), _one(layout="Rows")),
                    DISCONNECT,
                ],
            ),
            Scenario(
                "blocking_states_toggle_instead_of_cycling",
                steps=[
                    connect(_one()),
                    update(event("TogglePause"), _one(paused=True)),
                    {"callback": "next_layout"},
                    {"callback": "prev_layout"},
                    update(event("ToggleTiling"), _one(tile=False)),
                    {"callback": "next_layout"},
                    update(event("ToggleMaximize"), _one(maximized=True)),
                    {"callback": "prev_layout"},
                    update(event("ToggleMonocle"), _one(monocle=True)),
                    {"callback": "next_layout"},
                    {"callback": "toggle_monocle"},
                ],
            ),
            Scenario(
                "connect_aligns_cycling_list",
                steps=[
                    connect(_one(layout="VerticalStack")),
                    {"callback": "next_layout"},
                    {"callback": "first_layout"},
                    {"callback": "prev_layout"},
                ],
            ),
            Scenario(
                "label_template_and_flips",
                {"label": "{icon} {layout_name}"},
                steps=[
                    connect(_one()),
                    update(event("TogglePause"), _one(paused=True)),
                    update(event("ToggleTiling"), _one(tile=False)),
                    update(event("ChangeLayout"), _one(layout="Columns")),
                    {"callback": "flip_layout"},
                    {"callback": "flip_layout_vertical"},
                    {"callback": "flip_layout_horizontal_and_vertical"},
                    {"callback": "toggle_float"},
                    {"callback": "toggle_pause"},
                ],
            ),
            Scenario(
                "custom_layout_keeps_label",
                steps=[
                    connect(_one()),
                    update(event("ChangeLayout"), _one(layout={"Custom": []})),
                    update(event("ChangeLayout"), _one(layout="Columns")),
                ],
            ),
            Scenario(
                "shown_after_disconnect_when_not_hiding",
                {"hide_if_offline": False},
                steps=[DISCONNECT, connect(_one()), DISCONNECT],
            ),
            Scenario("other_monitor_ignored", steps=[connect(foreign_state())]),
            Scenario(
                "layout_list_without_bsp",
                {"layouts": ["columns", "rows"]},
                steps=[connect(_one(layout="Rows")), {"callback": "next_layout"}, {"callback": "next_layout"}],
            ),
        ],
    )
)
