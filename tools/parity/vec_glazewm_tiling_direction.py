"""Label vectors for the glazewm tiling_direction widget.

The real Python widget is built offscreen. GlazeWM is never contacted: `GlazewmClient.connect` is a
no-op and the client's `sendTextMessage` is replaced by a recorder, so the connection and direction
signals are emitted directly and each step also records the text frames the widget would have sent.

A step is {"connected": bool}, {"direction": "horizontal" | "vertical"} or {"click": true}.
"""

import gc
from typing import Any

from label_registry import LabelExporter, Scenario, register

_sent: list[str] = []


def _setup() -> Any:
    # Collect what the earlier exporters left behind now, while their Qt objects are still valid:
    # a collection that lands later (in the middle of a QWebSocket construction) crashes Python.
    gc.collect()
    from core.widgets.services.glazewm import client as module

    original = module.GlazewmClient.connect
    module.GlazewmClient.connect = lambda self: None  # type: ignore[method-assign]
    _sent.clear()

    def restore() -> None:
        module.GlazewmClient.connect = original  # type: ignore[method-assign]
        _sent.clear()

    return restore


def _build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.glazewm.tiling_direction import GlazewmTilingDirectionWidget

    widget = GlazewmTilingDirectionWidget(config)
    widget.glazewm_client._websocket.sendTextMessage = _sent.append
    return widget


def _step(widget: Any, step: dict[str, Any]) -> None:
    from core.widgets.services.glazewm.client import TilingDirection

    if "connected" in step:
        widget.glazewm_client.glazewm_connection_status.emit(step["connected"])
    elif "direction" in step:
        widget.glazewm_client.tiling_direction_processed.emit(TilingDirection(step["direction"]))
    elif step.get("click"):
        widget.tiling_direction_button.click()
    else:
        raise ValueError(f"unknown step {step}")


def _snapshot(widget: Any) -> dict[str, Any]:
    button = widget.tiling_direction_button
    return {
        "widget_hidden": widget.isHidden(),
        "frame_class": widget._widget_frame.property("class"),
        "button": {"text": button.text(), "class": button.property("class"), "hidden": button.isHidden()},
    }


def _teardown(widget: Any) -> None:
    from PyQt6 import sip

    client = widget.glazewm_client
    for owned in (client._websocket, client._reconnect_timer, client):
        sip.delete(owned)
    _sent.clear()


def _take_sent() -> list[str]:
    taken = list(_sent)
    _sent.clear()
    return taken


register(
    LabelExporter(
        widget="glazewm_tiling_direction",
        widget_type="glazewm.tiling_direction.GlazewmTilingDirectionWidget",
        build=_build,
        apply_step=_step,
        setup=_setup,
        teardown_widget=_teardown,
        snapshot=_snapshot,
        include_normalized=True,
        step_commands=_take_sent,
        scenarios=[
            Scenario(
                "defaults_connect_and_toggle",
                steps=[
                    {"direction": "vertical"},
                    {"connected": True},
                    {"direction": "horizontal"},
                    {"click": True},
                    {"direction": "vertical"},
                    {"click": True},
                    {"connected": False},
                    {"connected": True},
                ],
            ),
            Scenario(
                "custom_labels",
                {"horizontal_label": "H", "vertical_label": "V"},
                steps=[
                    {"connected": True},
                    {"direction": "horizontal"},
                    {"direction": "vertical"},
                    {"direction": "horizontal"},
                ],
            ),
            Scenario(
                "empty_labels_fall_back_to_direction",
                {"horizontal_label": "", "vertical_label": ""},
                steps=[
                    {"connected": True},
                    {"direction": "horizontal"},
                    {"direction": "vertical"},
                    {"connected": False},
                    {"direction": "horizontal"},
                ],
            ),
        ],
    )
)
