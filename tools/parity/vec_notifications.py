"""Label vectors for the notifications widget (yasb.notifications.NotificationsWidget).

Imported by vec_labels.py. The real widget is built offscreen; the WNF/do-not-disturb listener is
stubbed, and steps feed the widget the notification count the WNF service would emit.
"""

from typing import Any

from label_registry import LabelExporter, Scenario, register


class _FakeDnd:
    @staticmethod
    def initialize_wnf_listener() -> None:
        pass

    @staticmethod
    def get_status() -> str:
        return "unknown"


def _setup() -> Any:
    from core.widgets.yasb import notifications

    saved = notifications.DndService
    notifications.DndService = _FakeDnd

    def restore() -> None:
        notifications.DndService = saved

    return restore


def _build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.yasb.notifications import NotificationsWidget

    return NotificationsWidget(config)


def _apply(widget: Any, step: dict[str, Any]) -> None:
    if step.get("toggle"):
        widget._toggle_label()
    else:
        widget._on_windows_notification_update(step["count"])


def _count(value: int) -> dict[str, Any]:
    return {"count": value}


_COUNTS = [_count(1), _count(5), _count(9), _count(10), _count(250), _count(0)]

register(
    LabelExporter(
        widget="notifications",
        widget_type="yasb.notifications.NotificationsWidget",
        build=_build,
        apply_step=_apply,
        setup=_setup,
        include_normalized=True,
        scenarios=[
            Scenario("user_options_counts", steps=_COUNTS),
            Scenario(
                "user_options_alt_label_toggle",
                steps=[_count(3), {"toggle": True}, _count(12), _count(0), {"toggle": True}, _count(2)],
            ),
            Scenario(
                "hide_empty",
                {"hide_empty": True},
                steps=[_count(2), _count(0), _count(4), {"toggle": True}, _count(0), _count(1)],
            ),
            Scenario(
                "text_and_icon_label",
                {
                    "label": "<span>{icon}</span> {count} new",
                    "label_alt": "{count}+ <span class=\"extra\">{icon}</span>",
                },
                steps=[_count(1), _count(11), {"toggle": True}, _count(0), _count(7)],
            ),
            Scenario(
                "default_options",
                {"label": "{count} new notifications", "label_alt": "{count} new notifications",
                 "icons": {"new": "N", "default": "D"}},
                steps=[_count(2), {"toggle": True}, _count(0)],
            ),
        ],
    )
)
