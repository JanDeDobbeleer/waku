"""Label vectors for the control center widget (yasb.control_center.ControlCenterWidget).

The real widget and its five section widgets are built offscreen; only the system seams are faked:
the audio endpoints, the brightness service, theme, Focus Assist, power plans and modes, the touch
keyboard, the screenshot service, the power operations, the shell launcher and the media service.
Each fake reads one shared `STATE` dict, so a scenario step is a state patch plus an action, and
every side effect lands in `CALLS` (recorded as the step's "commands").

Scenario `initial` is a state patch applied before the widget is built; `{"open": True}` in it opens
the popup. The C++ test feeds its fakes the same state and compares the same snapshot.
"""

import copy
from typing import Any

from label_registry import LabelExporter, Scenario, register
from label_registry import snapshot as base_snapshot

HMONITOR = 111

DEFAULT_STATE: dict[str, Any] = {
    "output": {
        "present": True,
        "volume": 40,
        "muted": False,
        "devices": [["out-1", "Speakers"], ["out-2", "Headphones"]],
        "default": "out-1",
    },
    "input": {
        "present": True,
        "volume": 70,
        "muted": False,
        "devices": [["in-1", "Microphone"], ["in-2", "Headset Mic"]],
        "default": "in-1",
    },
    "brightness": {"monitors": [[HMONITOR, "Built-in display", ""]], "values": {str(HMONITOR): 55}},
    "theme": "dark",
    "dnd": "disabled",
    "plans": [["plan-balanced", "Balanced"], ["plan-perf", "High performance"]],
    "active_plan": "plan-balanced",
    "mode_supported": True,
    "active_mode": "Balanced",
    "media": None,
    "win10": False,
}
MODES = [("Best power efficiency", "mode-eff"), ("Balanced", "mode-bal"), ("Best performance", "mode-perf")]

STATE: dict[str, Any] = {}
CALLS: list[str] = []


def _merge(base: dict[str, Any], patch: dict[str, Any]) -> dict[str, Any]:
    merged = copy.deepcopy(base)
    for key, value in patch.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict) and key != "values":
            merged[key] = _merge(merged[key], value)
        else:
            merged[key] = copy.deepcopy(value)
    return merged


def _reset(initial: dict[str, Any] | None) -> None:
    STATE.clear()
    STATE.update(_merge(DEFAULT_STATE, {k: v for k, v in (initial or {}).items() if k != "open"}))
    CALLS.clear()


# --- fakes --------------------------------------------------------------------------------------


class _FakeInterface:
    def __init__(self, key: str) -> None:
        self.key = key

    def GetMasterVolumeLevelScalar(self) -> float:
        return STATE[self.key]["volume"] / 100

    def SetMasterVolumeLevelScalar(self, value: float, _context: Any) -> None:
        percent = round(value * 100)
        STATE[self.key]["volume"] = percent
        CALLS.append(f"volume:{self.key}:{percent}")

    def GetMute(self) -> int:
        return int(STATE[self.key]["muted"])

    def SetMute(self, value: Any, _context: Any) -> None:
        STATE[self.key]["muted"] = bool(value)
        CALLS.append(f"mute:{self.key}:{'on' if value else 'off'}")


def _fake_audio_classes() -> tuple[Any, Any]:
    class _Base:
        key = ""

        def register_widget(self, _widget: Any) -> None:
            pass

        def unregister_widget(self, _widget: Any) -> None:
            pass

        def _interface(self) -> Any:
            return _FakeInterface(self.key) if STATE[self.key]["present"] else None

        def get_all_devices(self) -> list[tuple[str, str]]:
            return [(i, n) for i, n in STATE[self.key]["devices"]]

        def get_default_device_id(self) -> str:
            return STATE[self.key]["default"]

        def set_default_device(self, device_id: str) -> bool:
            STATE[self.key]["default"] = device_id
            CALLS.append(f"default:{self.key}:{device_id}")
            return True

    class Output(_Base):
        key = "output"

        def get_volume_interface(self) -> Any:
            return self._interface()

    class Input(_Base):
        key = "input"

        def get_microphone_interface(self) -> Any:
            return self._interface()

    return Output, Input


def _fake_brightness_class() -> Any:
    from PyQt6.QtCore import QObject, pyqtSignal

    class FakeBrightness(QObject):
        brightness_changed = pyqtSignal(int, object)
        _instance: Any = None

        @classmethod
        def instance(cls, *_args: Any) -> Any:
            if cls._instance is None:
                cls._instance = cls()
            return cls._instance

        def get_brightness(self, hmonitor: int) -> int | None:
            return STATE["brightness"]["values"].get(str(hmonitor))

        def set_brightness(self, hmonitor: int, value: int) -> None:
            STATE["brightness"]["values"][str(hmonitor)] = value
            CALLS.append(f"brightness:{hmonitor}:{value}")

        def get_monitors(self) -> list[tuple[int, str]]:
            return [(m[0], m[1]) for m in STATE["brightness"]["monitors"]]

        def get_monitor_subtitle(self, hmonitor: int, index: int = 0) -> str:
            conn = next((m[2] for m in STATE["brightness"]["monitors"] if m[0] == hmonitor), "")
            return f"Monitor {index + 1} · {conn}" if conn else f"Monitor {index + 1}"

        def refresh_now(self, _hmonitor: int | None = None) -> None:
            pass

    return FakeBrightness


def _fake_media_class() -> Any:
    from PyQt6.QtCore import QObject, pyqtSignal

    class FakeMedia(QObject):
        current_session_changed = pyqtSignal()
        media_properties_changed = pyqtSignal()
        playback_info_changed = pyqtSignal()

        def __init__(self) -> None:
            super().__init__()
            self.current_session = None

        def play_pause(self) -> None:
            CALLS.append("media:play_pause")

        def prev(self) -> None:
            CALLS.append("media:prev")

        def next(self) -> None:
            CALLS.append("media:next")

        def switch_current_session(self, direction: int) -> None:
            CALLS.append(f"media:switch:{direction}")

    return FakeMedia


def _session_state(spec: dict[str, Any]) -> Any:
    from core.widgets.services.media.media import SessionState

    state = SessionState(spec.get("app_id", "Fake.Player.App"))
    state.title = spec.get("title", "Song")
    state.artist = spec.get("artist", "Artist")
    state.is_current = True
    state.is_playing = spec.get("playing", True)
    state.playback_status = 4 if state.is_playing else 5
    state.playback_ready = spec.get("playback_ready", True)
    state.controls_prev_enabled = spec.get("prev_enabled", True)
    state.controls_play_enabled = spec.get("play_enabled", True)
    state.controls_next_enabled = spec.get("next_enabled", True)
    return state


def _setup() -> Any:
    import core.widgets.services.control_center.sections.media as media_mod
    import core.widgets.services.control_center.sections.power as power_mod
    import core.widgets.services.control_center.sections.quick_actions as quick_mod
    import core.widgets.services.control_center.sections.system_controls as system_mod
    import core.widgets.yasb.control_center as center_mod

    Output, Input = _fake_audio_classes()
    saved = {
        center_mod: {n: getattr(center_mod, n) for n in ("AudioOutputService", "AudioInputService", "BrightnessService", "get_widget_monitor_hwnd")},
        media_mod: {"WindowsMedia": media_mod.WindowsMedia},
        power_mod: {n: getattr(power_mod, n) for n in ("PowerPlanService", "power_mode_api", "is_windows_10")},
        quick_mod: {n: getattr(quick_mod, n) for n in ("ThemeService", "DndService", "TouchKeyboardService", "ScreenshotService", "shell_open")},
        system_mod: {n: getattr(system_mod, n) for n in ("PowerOperations", "get_user_avatar_path", "get_windows_username", "shell_open")},
    }

    center_mod.AudioOutputService = Output
    center_mod.AudioInputService = Input
    center_mod.BrightnessService = _fake_brightness_class()
    center_mod.get_widget_monitor_hwnd = lambda _widget: HMONITOR
    media_mod.WindowsMedia = _fake_media_class()

    class FakePlanInfo:
        def __init__(self, guid: str, name: str) -> None:
            self.guid = guid
            self.name = name

    class FakePlanService:
        @classmethod
        def instance(cls) -> Any:
            return cls()

        def get_power_plans(self) -> tuple[list[Any], Any]:
            return [FakePlanInfo(g, n) for g, n in STATE["plans"]], STATE["active_plan"]

        def set_power_plan(self, guid: str) -> int:
            STATE["active_plan"] = guid
            CALLS.append(f"plan:{guid}")
            return 0

        @staticmethod
        def guids_equal(a: Any, b: Any) -> bool:
            return a == b

    class FakePowerMode:
        @staticmethod
        def get_modes() -> list[tuple[str, str]]:
            return list(MODES)

        @staticmethod
        def is_mode_supported() -> bool:
            return STATE["mode_supported"]

        @staticmethod
        def get_active_mode() -> Any:
            name = STATE["active_mode"]
            return (name, dict(MODES).get(name)) if name else None

        @staticmethod
        def set_mode(guid: str) -> bool:
            STATE["active_mode"] = next(n for n, g in MODES if g == guid)
            CALLS.append(f"mode:{guid}")
            return True

    power_mod.PowerPlanService = FakePlanService
    power_mod.power_mode_api = FakePowerMode
    power_mod.is_windows_10 = lambda: STATE["win10"]

    class FakeTheme:
        @staticmethod
        def get_theme_mode() -> str:
            return STATE["theme"]

        @staticmethod
        def toggle_theme_mode() -> str:
            STATE["theme"] = "light" if STATE["theme"] == "dark" else "dark"
            CALLS.append(f"theme:{STATE['theme']}")
            return STATE["theme"]

    class FakeDnd:
        @staticmethod
        def get_status() -> str:
            return STATE["dnd"]

        @staticmethod
        def set_status(mode: str) -> None:
            STATE["dnd"] = mode
            CALLS.append(f"dnd:{mode}")

    class FakeKeyboard:
        @staticmethod
        def toggle() -> None:
            CALLS.append("touch_keyboard")

    class FakeScreenshot:
        @staticmethod
        def start() -> None:
            CALLS.append("screenshot")

    def fake_shell_open(file: str, verb: str = "open", parameters: str | None = None, *_args: Any, **_kw: Any) -> None:
        CALLS.append(f"open:{file}" + (f":{parameters}" if parameters else ""))

    quick_mod.ThemeService = FakeTheme
    quick_mod.DndService = FakeDnd
    quick_mod.TouchKeyboardService = FakeKeyboard
    quick_mod.ScreenshotService = FakeScreenshot
    quick_mod.shell_open = fake_shell_open

    class FakePowerOps:
        def __init__(self, main_window: Any = None, overlay: Any = None) -> None:
            self.main_window = main_window

        def _record(self, name: str) -> None:
            if self.main_window is not None:
                self.main_window.hide()
            CALLS.append(f"power:{name}")

        def lock(self) -> None:
            self._record("lock")

        def signout(self) -> None:
            self._record("signout")

        def sleep(self) -> None:
            self._record("sleep")

        def hibernate(self) -> None:
            self._record("hibernate")

        def restart(self) -> None:
            self._record("restart")

        def shutdown(self) -> None:
            self._record("shutdown")

    system_mod.PowerOperations = FakePowerOps
    system_mod.get_user_avatar_path = lambda: None
    system_mod.get_windows_username = lambda: "Vector User"
    system_mod.shell_open = fake_shell_open

    def restore() -> None:
        for module, names in saved.items():
            for name, value in names.items():
                setattr(module, name, value)

    return restore


# --- widget driving -----------------------------------------------------------------------------


def _build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.yasb.control_center import ControlCenterWidget

    _reset(scenario.initial)
    widget = ControlCenterWidget(config)
    widget._vec_open = bool((scenario.initial or {}).get("open"))
    return widget


def _after_show(widget: Any) -> None:
    if widget._vec_open:
        widget._show_menu()


def _section(widget: Any, name: str) -> Any:
    return widget._section_widgets.get(name)


def _action_config(widget: Any, action_id: str) -> Any:
    return next(a for a in widget.config.sections.quick_actions.actions if a.id == action_id)


def _find_action(menu: Any, text: str) -> Any:
    return next(a for a in menu.actions() if a.text() == text)


def _apply(widget: Any, step: dict[str, Any]) -> None:
    CALLS.clear()
    if "state" in step:
        STATE.update(_merge(STATE, step["state"]))
        if widget.dialog is not None and widget.dialog.isVisible():
            widget._refresh_popup_state()
    if step.get("open"):
        widget._show_menu()
    if step.get("close"):
        widget.dialog.hide()
    if step.get("toggle_menu"):
        widget._toggle_menu()
    if "action" in step:
        quick = _section(widget, "quick_actions")
        quick._run_action(_action_config(widget, step["action"]))
    if "slider" in step:
        key, value = step["slider"]
        sliders = _section(widget, "sliders")
        getattr(sliders, f"_{key}_slider").setValue(value)
    if "source_menu" in step:
        sliders = _section(widget, "sliders")
        getattr(sliders, f"_show_{step['source_menu']}_source_menu")()
    if "source" in step:
        sliders = _section(widget, "sliders")
        key, value = step["source"]
        getattr(sliders, f"_set_{key}_source")(value)
    if "system_menu" in step:
        system = _section(widget, "system_controls")
        getattr(system, f"_show_{step['system_menu']}_menu")()
    if "system_action" in step:
        system = _section(widget, "system_controls")
        menu_name, text = step["system_action"]
        menu = getattr(system, f"_{menu_name}_menu")
        _find_action(menu, text).trigger()
    if "plan_menu" in step:
        power = _section(widget, "power")
        getattr(power, f"_show_power_{step['plan_menu']}_menu")()
    if "power_action" in step:
        power = _section(widget, "power")
        key, text = step["power_action"]
        _find_action(power._menus[key], text).trigger()
    if "media" in step:
        media_section = _section(widget, "media")
        media = media_section._media
        spec = step["media"]
        media.current_session = None if spec is None else _session_state(spec)
        media.current_session_changed.emit()
        if spec is not None:
            media.media_properties_changed.emit()
            media.playback_info_changed.emit()
    if step.get("media_hide"):
        _section(widget, "media")._hide_now()
    if "media_click" in step:
        media_section = _section(widget, "media")
        button = getattr(media_section, f"_{step['media_click']}_btn")
        button.data()


def _commands() -> list[str]:
    commands = list(CALLS)
    CALLS.clear()
    return commands


# --- snapshots ----------------------------------------------------------------------------------


def _label(label: Any) -> list[Any] | None:
    return None if label is None else [label.text(), label.property("class")]


def _menu_items(menu: Any) -> list[Any] | None:
    from core.utils.qobject import is_valid_qobject

    if menu is None or not is_valid_qobject(menu) or not menu.isVisible():
        return None
    return [[a.text(), a.isChecked()] if a.isCheckable() else a.text() for a in menu.actions()]


def _system_controls(section: Any) -> dict[str, Any]:
    return {
        "class": section.property("class"),
        "profile": section._profile_image_btn.property("class"),
        "settings": [section._settings_btn.text(), section._settings_btn.property("class")],
        "power": [section._power_btn.text(), section._power_btn.property("class")],
        "settings_menu": _menu_items(section._settings_menu),
        "profile_menu": _menu_items(section._profile_menu),
        "power_menu": _menu_items(section._power_menu),
    }


def _quick_actions(section: Any) -> dict[str, Any]:
    from PyQt6.QtWidgets import QLabel

    buttons = []
    for action_id, button in section._action_buttons.items():
        labels = button.findChildren(QLabel)
        icon = next(label for label in labels if "icon" in label.property("class").split())
        title = next(label for label in labels if "title" in label.property("class").split())
        buttons.append(
            {
                "id": action_id,
                "class": button.property("class"),
                "enabled": button.isEnabled(),
                "icon": _label(icon),
                "title": _label(title),
            }
        )
    return {"class": section.property("class"), "buttons": buttons}


def _sliders(section: Any) -> dict[str, Any]:
    rows: dict[str, Any] = {}
    for key in ("volume", "microphone", "brightness"):
        slider = getattr(section, f"_{key}_slider")
        if slider is None:
            continue
        source = getattr(section, f"_{key}_source_btn")
        rows[key] = {
            "class": slider.parent().property("class"),
            "value": slider.value(),
            "enabled": slider.isEnabled(),
            "text": getattr(section, f"_{key}_value_label").text(),
            "source": None if source is None else source.text(),
            "menu": _menu_items(getattr(section, f"_{key}_source_menu")),
        }
    return {"class": section.property("class"), "rows": rows}


def _power(section: Any) -> dict[str, Any]:
    mode_button = section._mode_button
    return {
        "class": section.property("class"),
        "plan_class": section._action_button.property("class"),
        "plan": section._plan_label.text(),
        "mode_class": None if mode_button is None else mode_button.property("class"),
        "mode": None if section._mode_label is None else section._mode_label.text(),
        "plan_menu": _menu_items(section._menus.get("plan")),
        "mode_menu": _menu_items(section._menus.get("mode")),
    }


def _media(section: Any) -> dict[str, Any]:
    return {
        "class": section.property("class"),
        "hidden": section.isHidden(),
        "title": section._title_label.text(),
        "artist": section._artist_label.text(),
        "thumbnail_has_pixmap": not section._thumbnail_label.pixmap().isNull(),
        "prev": [section._prev_btn.text(), section._prev_btn.property("class")],
        "play": [section._play_btn.text(), section._play_btn.property("class")],
        "next": [section._next_btn.text(), section._next_btn.property("class")],
    }


_SNAPSHOTS = {
    "system_controls": _system_controls,
    "quick_actions": _quick_actions,
    "sliders": _sliders,
    "power": _power,
    "media": _media,
}


def _menu(widget: Any) -> dict[str, Any] | None:
    dialog = widget.dialog
    if dialog is None or not dialog.isVisible():
        return None
    sections = {name: _SNAPSHOTS[name](section) for name, section in widget._section_widgets.items()}
    return {"class": dialog.property("class"), "order": list(sections), "sections": sections}


def _snapshot(widget: Any) -> dict[str, Any]:
    snap = base_snapshot(widget)
    snap["menu"] = _menu(widget)
    return snap


# --- scenarios ----------------------------------------------------------------------------------

NO_BLUR = {"popup": {"blur": False}}


def _scenario(name: str, patch: dict[str, Any] | None = None, **kwargs: Any) -> Scenario:
    options = {**NO_BLUR, **(patch or {})}
    if patch and "popup" in patch:
        options["popup"] = {**NO_BLUR["popup"], **patch["popup"]}
    # The vector carries the complete starting state so the C++ test needs no copy of the defaults.
    initial = kwargs.pop("initial", None) or {}
    state = _merge(DEFAULT_STATE, {k: v for k, v in initial.items() if k != "open"})
    state["open"] = bool(initial.get("open"))
    return Scenario(name, options, initial=state, **kwargs)


def _teardown(widget: Any) -> None:
    if widget.dialog is not None:
        widget.dialog.hide()


register(
    LabelExporter(
        widget="control_center",
        widget_type="yasb.control_center.ControlCenterWidget",
        build=_build,
        apply_step=_apply,
        setup=_setup,
        snapshot=_snapshot,
        after_show=_after_show,
        step_commands=_commands,
        teardown_widget=_teardown,
        include_normalized=True,
        scenarios=[
            _scenario("closed_label", steps=[{"toggle_menu": True}, {"toggle_menu": True}]),
            _scenario(
                "user_open_default",
                initial={"open": True},
                steps=[
                    {"state": {"output": {"volume": 10}, "input": {"volume": 20}}},
                    {"state": {"brightness": {"values": {str(HMONITOR): 80}}}},
                    {"close": True},
                    {"open": True},
                ],
            ),
            _scenario(
                "quick_actions",
                initial={"open": True},
                steps=[
                    {"action": "toggle_mute"},
                    {"action": "toggle_mute"},
                    {"action": "toggle_mic_mute"},
                    {"action": "toggle_dnd"},
                    {"action": "toggle_dnd"},
                    {"action": "toggle_theme"},
                    {"action": "touch_keyboard"},
                    {"action": "screenshot"},
                    {"open": True},
                    {"state": {"output": {"present": False}, "input": {"present": False}}},
                    {"action": "toggle_mute"},
                    {"state": {"output": {"present": True, "muted": True}, "dnd": "alarms", "theme": "light"}},
                ],
            ),
            _scenario(
                "quick_actions_inline_custom",
                {
                    "sections": {
                        "quick_actions": {
                            "columns": 2,
                            "label_position": "inline",
                            "actions": [
                                {"id": "cycle_dnd", "label": "Focus", "icon": "F"},
                                {"id": "custom", "label": "Calc", "icon": "C", "command": "calc.exe --flag value"},
                                {"id": "toggle_theme", "label": "Theme", "icon": "T"},
                                {"id": "mystery", "label": "Mystery", "icon": "?"},
                            ],
                        }
                    }
                },
                initial={"open": True},
                steps=[
                    {"action": "cycle_dnd"},
                    {"action": "cycle_dnd"},
                    {"action": "cycle_dnd"},
                    {"action": "custom"},
                    {"action": "mystery"},
                    {"state": {"dnd": "unknown"}},
                ],
            ),
            _scenario(
                "volume_and_mic_sliders",
                initial={"open": True, "output": {"muted": True}},
                steps=[
                    {"slider": ["volume", 55]},
                    {"slider": ["volume", 0]},
                    {"slider": ["volume", 30]},
                    {"slider": ["microphone", 25]},
                    {"state": {"output": {"present": False}}},
                    {"state": {"output": {"present": True, "volume": 90}, "input": {"present": False}}},
                ],
            ),
            _scenario(
                "source_selectors",
                initial={"open": True},
                steps=[
                    {"source_menu": "volume"},
                    {"source": ["volume", "out-2"]},
                    {"source_menu": "microphone"},
                    {"source": ["microphone", "in-2"]},
                    {"source_menu": "brightness"},
                    {
                        "state": {
                            "brightness": {
                                "monitors": [[HMONITOR, "Built-in display", ""], [222, "DELL U2720Q", "Hdmi"]],
                                "values": {str(HMONITOR): 55, "222": 30},
                            }
                        }
                    },
                    {"source_menu": "brightness"},
                    {"source": ["brightness", 222]},
                    {"slider": ["brightness", 45]},
                    {"state": {"brightness": {"values": {"222": None}}}},
                ],
            ),
            _scenario(
                "sliders_hidden_parts",
                {
                    "sections": {
                        "sliders": {
                            "microphone": {"show_slider": False},
                            "volume": {"show_source_selector": False},
                        }
                    }
                },
                initial={"open": True},
                steps=[{"slider": ["volume", 60]}],
            ),
            _scenario(
                "power_section",
                initial={"open": True},
                steps=[
                    {"plan_menu": "plan"},
                    {"power_action": ["plan", "High performance"]},
                    {"plan_menu": "mode"},
                    {"power_action": ["mode", "Best performance"]},
                    {"state": {"mode_supported": False, "active_mode": None, "active_plan": "plan-balanced"}},
                    {"plan_menu": "mode"},
                    {"state": {"plans": [], "active_plan": None}},
                ],
            ),
            _scenario(
                "power_section_windows_10",
                initial={"open": True, "win10": True},
                steps=[{"plan_menu": "plan"}],
            ),
            _scenario(
                "system_controls_menus",
                initial={"open": True},
                steps=[
                    {"system_menu": "settings"},
                    {"system_action": ["settings", "Sound"]},
                    {"system_menu": "profile"},
                    {"system_action": ["profile", "Lock"]},
                    {"open": True},
                    {"system_menu": "power"},
                    {"system_action": ["power", "Restart"]},
                    {"open": True},
                    {"system_menu": "profile"},
                    {"system_action": ["profile", "Vector User"]},
                ],
            ),
            _scenario(
                "media_section",
                initial={"open": True},
                steps=[
                    {"media": {"title": "Song", "artist": "Artist", "playing": True}},
                    {"media": {"title": "Song", "artist": "Artist", "playing": False, "next_enabled": False}},
                    {"media": {"title": "", "artist": "", "playing": False, "playback_ready": False}},
                    {"media_click": "play"},
                    {"media_click": "prev"},
                    {"media_click": "next"},
                    {"media": None},
                    {"media_hide": True},
                ],
            ),
            _scenario(
                "sections_order_and_hidden",
                {
                    "sections_order": ["media", "power", "quick_actions"],
                    "sections": {"power": {"show": False}, "media": {"icons": {"play": "P", "pause": "Z"}}},
                },
                initial={"open": True},
                steps=[{"media": {"title": "A", "artist": "B", "playing": True}}],
            ),
        ],
    )
)
