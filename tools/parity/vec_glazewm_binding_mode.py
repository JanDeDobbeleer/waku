"""Label vectors for the glazewm binding mode widget.

The real Python widget is built offscreen. GlazeWM itself is never contacted: `GlazewmClient` is
replaced by a stub that records the commands the widget sends, and steps call the widget's slots
directly with the values the client's signals would carry.
"""

from typing import Any

from label_registry import LabelExporter, Scenario, register

_commands: list[str] = []


class _Signal:
    def connect(self, _slot: Any) -> None:
        pass


class _FakeClient:
    """Stands in for GlazewmClient: no socket, commands are recorded instead of sent."""

    def __init__(self, *_args: Any, **_kwargs: Any) -> None:
        self.glazewm_connection_status = _Signal()
        self.binding_mode_changed = _Signal()

    def connect(self) -> None:
        pass

    def disable_binding_mode(self, name: str) -> None:
        _commands.append(f"command wm-disable-binding-mode --name {name}")

    def enable_binding_mode(self, name: str) -> None:
        _commands.append(f"command wm-enable-binding-mode --name {name}")


def _setup() -> Any:
    from core.widgets.glazewm import binding_mode

    original = binding_mode.GlazewmClient
    binding_mode.GlazewmClient = _FakeClient  # type: ignore[misc,assignment]
    _commands.clear()

    def restore() -> None:
        binding_mode.GlazewmClient = original  # type: ignore[misc]
        _commands.clear()

    return restore


def _build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.glazewm.binding_mode import GlazewmBindingModeWidget

    return GlazewmBindingModeWidget(config)


def _step(widget: Any, step: dict[str, Any]) -> None:
    if "connection" in step:
        widget._update_connection_status(step["connection"])
    elif "mode" in step:
        from core.widgets.services.glazewm.client import BindingMode

        mode = step["mode"] or {}
        widget._update_binding_mode(BindingMode(mode.get("name"), mode.get("display_name")))
    elif "callback" in step:
        widget._run_callback(step["callback"])
    else:
        raise ValueError(f"unknown step {step}")


def _take_commands() -> list[str]:
    taken = list(_commands)
    _commands.clear()
    return taken


def mode(name: str | None = None, display_name: str | None = None) -> dict[str, Any]:
    return {"mode": {"name": name, "display_name": display_name}}


NONE = mode()
RESIZE = mode("resize", "Resize")
PAUSE = mode("pause")
CUSTOM = mode("custom")

register(
    LabelExporter(
        widget="glazewm_binding_mode",
        widget_type="glazewm.binding_mode.GlazewmBindingModeWidget",
        build=_build,
        apply_step=_step,
        setup=_setup,
        include_normalized=True,
        step_commands=_take_commands,
        scenarios=[
            Scenario(
                "active_and_inactive_modes",
                steps=[
                    RESIZE,
                    PAUSE,
                    CUSTOM,
                    NONE,
                    RESIZE,
                    {"connection": False},
                    {"connection": True},
                    PAUSE,
                ],
            ),
            Scenario(
                "show_when_no_active",
                {"hide_if_no_active": False},
                steps=[NONE, RESIZE, NONE, mode("", ""), {"connection": False}, NONE],
            ),
            Scenario(
                "alt_label_toggle",
                steps=[
                    RESIZE,
                    {"callback": "toggle_label"},
                    PAUSE,
                    CUSTOM,
                    {"callback": "toggle_label"},
                    NONE,
                    RESIZE,
                ],
            ),
            Scenario(
                "alt_label_toggle_when_inactive_is_shown",
                {"hide_if_no_active": False},
                steps=[NONE, {"callback": "toggle_label"}, RESIZE, {"callback": "toggle_label"}, NONE],
            ),
            Scenario(
                "toggle_before_first_event",
                steps=[{"callback": "toggle_label"}, RESIZE, {"callback": "toggle_label"}],
            ),
            Scenario(
                "cycling_next_prev",
                steps=[
                    {"callback": "next_binding_mode"},
                    {"callback": "next_binding_mode"},
                    {"callback": "next_binding_mode"},
                    {"callback": "next_binding_mode"},
                    {"callback": "prev_binding_mode"},
                    {"callback": "prev_binding_mode"},
                    {"callback": "prev_binding_mode"},
                    PAUSE,
                    {"callback": "next_binding_mode"},
                    {"callback": "prev_binding_mode"},
                    {"callback": "prev_binding_mode"},
                    NONE,
                    {"callback": "next_binding_mode"},
                    RESIZE,
                    {"callback": "prev_binding_mode"},
                    {"callback": "prev_binding_mode"},
                    {"callback": "disable_binding_mode"},
                ],
            ),
            Scenario(
                "cycling_with_active_mode_outside_list",
                steps=[CUSTOM, {"callback": "next_binding_mode"}, {"callback": "prev_binding_mode"}],
            ),
            Scenario(
                "cycling_disabled_when_list_empty",
                {"binding_modes_to_cycle_through": []},
                steps=[RESIZE, {"callback": "next_binding_mode"}, {"callback": "prev_binding_mode"}],
            ),
            Scenario(
                "cycling_without_none_entry",
                {"binding_modes_to_cycle_through": ["resize", "pause"]},
                steps=[
                    RESIZE,
                    {"callback": "next_binding_mode"},
                    NONE,
                    {"callback": "next_binding_mode"},
                    {"callback": "prev_binding_mode"},
                ],
            ),
            Scenario(
                "disable_binding_mode_callback",
                steps=[
                    {"callback": "disable_binding_mode"},
                    RESIZE,
                    {"callback": "disable_binding_mode"},
                    NONE,
                    {"callback": "disable_binding_mode"},
                ],
            ),
            Scenario(
                "disable_after_hidden_none_uses_last_active_mode",
                steps=[PAUSE, NONE, {"callback": "disable_binding_mode"}],
            ),
            Scenario(
                "custom_label_and_icons",
                {
                    "label": "<span>{icon}</span> {binding_mode}!<span class='x'>fixed</span>",
                    "label_alt": "mode={binding_mode} <span>{binding_mode}</span>",
                    "label_if_no_active": "idle",
                    "default_icon": "?",
                    "icons": {"none": "-", "resize": "R"},
                    "hide_if_no_active": False,
                },
                steps=[
                    NONE,
                    RESIZE,
                    PAUSE,
                    mode("custom", "Shown Name"),
                    {"callback": "toggle_label"},
                    NONE,
                    RESIZE,
                    PAUSE,
                ],
            ),
            Scenario(
                "label_without_icon_or_placeholders",
                {
                    "label": "{binding_mode}",
                    "label_alt": "",
                    "hide_if_no_active": False,
                },
                steps=[NONE, RESIZE, {"callback": "toggle_label"}, PAUSE, NONE],
            ),
        ],
    )
)
