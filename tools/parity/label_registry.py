"""Registry and runner for label vectors.

Each widget registers a `LabelExporter`: how to build the real widget offscreen with fake
service data, how to apply a scenario step, and its scenarios. Adding a widget in a later
unit means adding one module that calls `register`, and importing it in vec_labels.py.
"""

import copy
from collections.abc import Callable
from dataclasses import dataclass, field
from importlib import import_module
from typing import Any

LABEL_EXPORTERS: dict[str, "LabelExporter"] = {}


@dataclass
class Scenario:
    name: str
    options_patch: dict[str, Any] = field(default_factory=dict)
    initial: Any = None
    steps: list[dict[str, Any]] = field(default_factory=list)


@dataclass
class LabelExporter:
    widget: str
    widget_type: str
    build: Callable[[Any, Scenario], Any]
    apply_step: Callable[[Any, dict[str, Any]], None]
    scenarios: list[Scenario]
    setup: Callable[[], Callable[[], None]] | None = None
    teardown_widget: Callable[[Any], None] | None = None
    after_show: Callable[[Any], None] | None = None


def register(exporter: LabelExporter) -> None:
    LABEL_EXPORTERS[exporter.widget] = exporter


def deep_merge(base: dict[str, Any], patch: dict[str, Any]) -> dict[str, Any]:
    merged = copy.deepcopy(base)
    for key, value in patch.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = deep_merge(merged[key], value)
        else:
            merged[key] = copy.deepcopy(value)
    return merged


def _label_state(label: Any) -> dict[str, Any]:
    return {"text": label.text(), "class": label.property("class"), "hidden": label.isHidden()}


def snapshot(widget: Any) -> dict[str, Any]:
    layout = widget._widget_container_layout
    ordered = [layout.itemAt(i).widget() for i in range(layout.count())]
    return {
        "widget_hidden": widget.isHidden(),
        "frame_class": widget._widget_frame.property("class"),
        "container_class": widget._widget_container.property("class"),
        "primary": [_label_state(label) for label in widget._widgets],
        "alt": [_label_state(label) for label in widget._widgets_alt],
        "layout_order": [
            "primary" if label in widget._widgets else "alt" if label in widget._widgets_alt else "other"
            for label in ordered
        ],
    }


def _run(exporter: LabelExporter, scenario: Scenario, base_options: dict[str, Any]) -> dict[str, Any]:
    module_name, class_name = exporter.widget_type.rsplit(".", 1)
    widget_cls = getattr(import_module(f"core.widgets.{module_name}"), class_name)
    options = deep_merge(base_options, scenario.options_patch)
    entry: dict[str, Any] = {
        "widget": exporter.widget,
        "scenario": scenario.name,
        "options": options,
        "initial_data": scenario.initial,
        "steps": [],
    }
    config = widget_cls.validation_schema.model_validate(copy.deepcopy(options))
    try:
        widget = exporter.build(config, scenario)
    except Exception as error:
        entry["construct_error"] = type(error).__name__
        return entry
    try:
        widget.show()
        if exporter.after_show:
            exporter.after_show(widget)
        entry["initial"] = snapshot(widget)
        for step in scenario.steps:
            record: dict[str, Any] = {"step": step}
            try:
                exporter.apply_step(widget, step)
            except Exception as error:
                record["error"] = type(error).__name__
            record["snapshot"] = snapshot(widget)
            entry["steps"].append(record)
    finally:
        if exporter.teardown_widget:
            exporter.teardown_widget(widget)
        widget.hide()
    return entry


def export_label_vectors(user_options: dict[str, dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    """`user_options` maps a widget type (e.g. "yasb.cpu.CpuWidget") to the user's raw options."""
    vectors: dict[str, list[dict[str, Any]]] = {}
    for name, exporter in sorted(LABEL_EXPORTERS.items()):
        restore = exporter.setup() if exporter.setup else None
        try:
            base = user_options.get(exporter.widget_type, {})
            vectors[name] = [_run(exporter, scenario, base) for scenario in exporter.scenarios]
        finally:
            if restore:
                restore()
    return vectors
