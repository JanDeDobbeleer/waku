"""Label vectors for the systray widget.

Imported by vec_labels.py. The real SystrayWidget is built offscreen. The tray client is never
created (`setup_client` is replaced by a no-op), so no hook, no monitor window and no Explorer
interaction happens; steps feed `IconData` straight into the widget slots. The pin state file lives
in the sandbox's LOCALAPPDATA (see sandbox.py).
"""

import json
from typing import Any
from uuid import UUID

from label_registry import LabelExporter, Scenario, register

NIF = {"message": 0x1, "icon": 0x2, "tip": 0x4, "state": 0x8, "info": 0x10, "guid": 0x20}

BATTERY_GUID = "7820ae75-23e3-4229-82c1-e41cb67d5b9c"
VOLUME_GUID = "7820ae73-23e3-4229-82c1-e41cb67d5b9c"
NETWORK_GUID = "7820ae74-23e3-4229-82c1-e41cb67d5b9c"
APP_GUID = "11111111-2222-3333-4444-555555555555"


def _state_path(widget: Any) -> Any:
    from core.utils.system import app_data_path

    return app_data_path(f"systray_state_{widget.screen_id}.json")


def _setup() -> Any:
    from core.widgets.yasb import systray

    original = systray.SystrayWidget.setup_client
    systray.SystrayWidget.setup_client = lambda self: None  # type: ignore[method-assign]

    def restore() -> None:
        systray.SystrayWidget.setup_client = original  # type: ignore[method-assign]

    return restore


def _state_file_name() -> str:
    from PyQt6.QtWidgets import QApplication

    from core.utils.system import app_data_path

    probe = QApplication.primaryScreen()
    import re

    raw_id = f"{probe.manufacturer()}{probe.name()}{probe.serialNumber()}".upper()
    return str(app_data_path(f"systray_state_{re.sub(r'\W+', '', raw_id)}.json"))


def _build(config: Any, scenario: Scenario) -> Any:
    from pathlib import Path

    from core.widgets.yasb.systray import SystrayWidget

    path = Path(_state_file_name())
    path.unlink(missing_ok=True)
    if scenario.initial is not None:
        # "initial" is a list of [key, state] pairs: the vector file sorts object keys, the order
        # of the saved entries matters.
        path.write_text(json.dumps(dict(scenario.initial), indent=2), encoding="utf-8")
    widget = SystrayWidget(config)
    # The widget schedules this for the event loop, which the exporter does not run.
    widget.load_state()
    return widget


def _teardown(widget: Any) -> None:
    from pathlib import Path

    if widget.config.show_in_popup:
        widget._systray_popup.hide()
    Path(_state_file_name()).unlink(missing_ok=True)


def _icon_data(spec: dict[str, Any]) -> Any:
    from PyQt6.QtGui import QColor, QImage

    from core.widgets.services.systray.systray_monitor import IconData

    flags = 0
    for name in spec.get("flags", ["message", "icon", "tip"]):
        flags |= NIF[name]
    guid = spec.get("guid")
    if guid:
        flags |= NIF["guid"]
    data = IconData(
        message_type=0,
        hWnd=spec["hwnd"],
        uID=spec.get("uid", 1),
        guid=UUID(guid) if guid else None,
        uFlags=flags,
        dwState=spec.get("state", 0),
        szTip=spec.get("tip", ""),
        uCallbackMessage=0x8000 if flags & NIF["message"] else 0,
        uVersion=spec.get("version", 0),
        exe=spec["exe"],
        exe_path=f"C:\\Apps\\{spec['exe']}.exe",
    )
    if flags & NIF["icon"]:
        data.hIcon = 1
        image = QImage(32, 32, QImage.Format.Format_ARGB32)
        image.fill(QColor(200, 30, 30))
        data.icon_image = image
    return data


def _find(widget: Any, exe: str) -> Any:
    for icon in widget.icons:
        if icon.data is not None and icon.data.exe == exe:
            return icon
    raise KeyError(exe)


def _step(widget: Any, step: dict[str, Any]) -> None:
    if "icon" in step:
        widget.on_icon_modified(_icon_data(step["icon"]))
    elif "delete" in step:
        from core.widgets.services.systray.systray_monitor import IconData

        spec = step["delete"]
        widget.on_icon_deleted(
            IconData(hWnd=spec["hwnd"], uID=spec.get("uid", 1), guid=UUID(spec["guid"]) if spec.get("guid") else None)
        )
    elif "pin" in step:
        widget.on_icon_pinned_changed(_find(widget, step["pin"]))
    elif "sort" in step:
        widget.sort_icons()
    elif "save" in step:
        widget.save_state()
    elif "load" in step:
        widget.load_state()
    elif "update_pinned_visibility" in step:
        widget.update_pinned_widget_visibility(force_show=step["update_pinned_visibility"])
    elif "drag_started" in step:
        spec = step["drag_started"]
        widget.on_drag_started(spec["width"], from_pinned=spec["pinned"])
    elif "drag_ended" in step:
        widget.on_drag_ended()
    elif "toggle_button" in step:
        widget.unpinned_vis_btn.click()
    elif "hide_popup" in step:
        widget._systray_popup.hide()
    else:
        raise ValueError(step)


def _icons_in(layout: Any) -> list[Any]:
    from core.widgets.services.systray.systray_widget import IconWidget

    result = []
    for index in range(layout.count()):
        item = layout.itemAt(index)
        widget = item.widget() if item is not None else None
        if isinstance(widget, IconWidget):
            result.append(widget)
    return result


def _icon_state(icon: Any) -> dict[str, Any]:
    tooltip = getattr(icon, "_tooltip_filter", None)
    return {
        "exe": icon.data.exe,
        "key": str(icon.data.guid) if icon.data.guid is not None else icon.data.exe_path,
        "hidden": icon.isHidden(),
        "class": icon.property("class"),
        "pinned": icon.is_pinned,
        "tooltip": tooltip.tooltip_text if tooltip is not None else None,
        "has_icon": not icon.icon().isNull(),
    }


def _snapshot(widget: Any) -> dict[str, Any]:
    from pathlib import Path

    popup = widget.config.show_in_popup
    path = Path(_state_file_name())
    state_text = path.read_bytes().decode("utf-8") if path.exists() else None
    layout = widget.widget_container_layout
    return {
        "widget_hidden": widget.isHidden(),
        "frame_class": widget._widget_frame.property("class"),
        "container_class": widget.widget_container.property("class"),
        "container_order": [
            "pinned"
            if layout.itemAt(i).widget() is widget.pinned_widget
            else "button"
            if layout.itemAt(i).widget() is widget.unpinned_vis_btn
            else "unpinned"
            for i in range(layout.count())
        ],
        "button": {
            "text": widget.unpinned_vis_btn.text(),
            "checked": widget.unpinned_vis_btn.isChecked(),
            "hidden": widget.unpinned_vis_btn.isHidden(),
            "class": widget.unpinned_vis_btn.property("class"),
        },
        "pinned_container": {
            "class": widget.pinned_widget.property("class"),
            "hidden": widget.pinned_widget.isHidden(),
            "forceshow": widget.pinned_widget.property("forceshow"),
            "min_width": widget.pinned_widget.minimumWidth(),
        },
        "unpinned_container": None
        if popup
        else {"class": widget.unpinned_widget.property("class"), "hidden": widget.unpinned_widget.isHidden()},
        "popup_open": widget._systray_popup.is_visible if popup else None,
        "pinned": [_icon_state(icon) for icon in _icons_in(widget.pinned_layout)],
        "unpinned": [_icon_state(icon) for icon in _icons_in(widget.unpinned_layout)],
        "icon_count": len(widget.icons),
        "state": [[key, {"is_pinned": s.is_pinned, "index": s.index}] for key, s in widget.current_state.items()],
        "state_file": state_text,
        "state_file_name": path.name,
    }


def _icon(exe: str, hwnd: int, **extra: Any) -> dict[str, Any]:
    return {"icon": {"exe": exe, "hwnd": hwnd, **extra}}


def _saved(**entries: tuple[bool, int]) -> list[list[Any]]:
    return [[f"C:\\Apps\\{exe}.exe", {"is_pinned": pinned, "index": index}] for exe, (pinned, index) in entries.items()]


_INLINE = {"show_in_popup": False, "show_unpinned": True}

register(
    LabelExporter(
        widget="systray",
        widget_type="yasb.systray.SystrayWidget",
        build=_build,
        apply_step=_step,
        setup=_setup,
        teardown_widget=_teardown,
        snapshot=_snapshot,
        include_normalized=True,
        scenarios=[
            Scenario(
                "user_options_popup_mode",
                steps=[
                    _icon("Discord", 10, tip="Discord - 3 unread"),
                    _icon("Steam", 11, tip="Steam"),
                    _icon("Slack", 12),
                    _icon("Battery", 13, guid=BATTERY_GUID),
                    _icon("Volume", 14, guid=VOLUME_GUID),
                    _icon("Network", 15, guid=NETWORK_GUID),
                    {"pin": "Steam"},
                    {"sort": True},
                    {"save": True},
                    {"pin": "Steam"},
                    {"delete": {"hwnd": 11}},
                    {"save": True},
                ],
            ),
            Scenario(
                "inline_mode_pin_and_unpin",
                dict(_INLINE),
                steps=[
                    _icon("A", 1),
                    _icon("B", 2),
                    _icon("C", 3),
                    {"pin": "B"},
                    {"save": True},
                    {"pin": "C"},
                    {"save": True},
                    {"pin": "B"},
                    {"update_pinned_visibility": False},
                    {"pin": "C"},
                    {"update_pinned_visibility": False},
                    {"save": True},
                ],
            ),
            Scenario(
                "saved_state_places_and_orders",
                dict(_INLINE),
                initial=_saved(Pinned=(True, 0), Second=(False, 1), First=(False, 0)),
                steps=[
                    _icon("Second", 1),
                    _icon("Pinned", 2),
                    _icon("First", 3),
                    _icon("Fresh", 4),
                    {"sort": True},
                    {"save": True},
                ],
            ),
            Scenario(
                "saved_state_is_renumbered_on_load",
                dict(_INLINE),
                initial=_saved(A=(True, 7), B=(True, 3), C=(False, 9), D=(False, 2)),
                steps=[
                    {"save": True},
                    _icon("D", 1),
                    _icon("B", 2),
                    _icon("A", 3),
                    {"sort": True},
                    {"save": True},
                ],
            ),
            Scenario(
                "saved_state_by_guid",
                dict(_INLINE),
                initial=[
                    [APP_GUID, {"is_pinned": True, "index": 0}],
                    ["C:\\Apps\\Same.exe", {"is_pinned": False, "index": 1}],
                ],
                steps=[_icon("Same", 1, guid=APP_GUID), _icon("Same", 2), {"save": True}],
            ),
            Scenario(
                "windows_icons_and_hide_icons",
                {**_INLINE, "show_volume": True, "hide_icons": ["Discord", "steam"]},
                steps=[
                    _icon("Battery", 1, guid=BATTERY_GUID),
                    _icon("Volume", 2, guid=VOLUME_GUID),
                    _icon("Network", 3, guid=NETWORK_GUID),
                    _icon("discord", 4),
                    _icon("Steam", 5),
                    _icon("Other", 6),
                ],
            ),
            Scenario(
                "show_battery_and_network",
                {**_INLINE, "show_battery": True, "show_network": True},
                steps=[
                    _icon("Battery", 1, guid=BATTERY_GUID),
                    _icon("Volume", 2, guid=VOLUME_GUID),
                    _icon("Network", 3, guid=NETWORK_GUID),
                ],
            ),
            Scenario(
                "hidden_state_flag",
                dict(_INLINE),
                steps=[
                    _icon("A", 1, flags=["message", "icon", "tip", "state"], state=1),
                    _icon("B", 2),
                    _icon("A", 1, flags=["state"], state=0),
                    {"save": True},
                    _icon("A", 1, flags=["state"], state=1),
                    {"save": True},
                ],
            ),
            Scenario(
                "modify_updates_same_icon",
                dict(_INLINE),
                steps=[
                    _icon("A", 1, tip="first", guid=APP_GUID),
                    _icon("A", 99, tip="second", guid=APP_GUID),
                    _icon("B", 2, tip=""),
                    _icon("B", 2, flags=["tip"], tip="renamed"),
                    _icon("C", 3, flags=["message", "icon"]),
                ],
            ),
            Scenario(
                "inline_without_button_and_left_label",
                {**_INLINE, "show_unpinned": False, "show_unpinned_button": False, "label_position": "left"},
                steps=[_icon("A", 1), {"toggle_button": True}],
            ),
            Scenario(
                "inline_collapsed_button_toggles",
                {**_INLINE, "show_unpinned": False, "label_position": "left"},
                steps=[_icon("A", 1), {"toggle_button": True}, {"toggle_button": True}],
            ),
            Scenario(
                "drag_state_popup_mode",
                steps=[
                    _icon("A", 1),
                    {"drag_started": {"width": 24, "pinned": True}},
                    {"drag_started": {"width": 0, "pinned": False}},
                    {"drag_ended": True},
                    {"hide_popup": True},
                    {"pin": "A"},
                    {"drag_started": {"width": 24, "pinned": False}},
                    {"drag_ended": True},
                ],
            ),
            Scenario(
                "drag_state_inline_mode",
                dict(_INLINE),
                steps=[
                    {"drag_started": {"width": 22, "pinned": False}},
                    {"drag_ended": True},
                    _icon("A", 1),
                    {"pin": "A"},
                    {"drag_started": {"width": 22, "pinned": True}},
                    {"drag_ended": True},
                ],
            ),
            Scenario(
                "foreign_state_keys_survive_save",
                dict(_INLINE),
                initial=[*_saved(A=(True, 0)), ["C:\\Elsewhere\\Other.exe", {"is_pinned": False, "index": 3}]],
                steps=[_icon("A", 1), _icon("B", 2), {"pin": "B"}, {"save": True}],
            ),
            Scenario(
                "non_ascii_executable_path",
                dict(_INLINE),
                steps=[_icon("Caf\u00e9", 1, tip="Caf\u00e9 \u2615"), {"pin": "Caf\u00e9"}, {"save": True}],
            ),
        ],
    )
)
