"""Label vectors for the widgets ported so far: cpu, memory, battery.

Later units add a widget by writing a `register(LabelExporter(...))` block (or a module that
does) and listing it here. The widget classes are the real ones, built offscreen; only the
service data is faked.
"""

from typing import Any

from label_registry import LABEL_EXPORTERS, LabelExporter, Scenario, export_label_vectors, register

GIB = 1024**3
SENTINEL_WORKER = object()


def _reset_instances(cls: Any) -> None:
    cls._instances.clear()


def _stub_worker(cls_path: str) -> Any:
    module_name, class_name = cls_path.rsplit(".", 1)
    from importlib import import_module

    cls = getattr(import_module(f"core.widgets.{module_name}"), class_name)

    def setup() -> Any:
        previous = cls._worker
        cls._worker = SENTINEL_WORKER

        def restore() -> None:
            cls._worker = previous

        return restore

    return setup


# --- cpu -------------------------------------------------------------------


def _cpu_data(step: dict[str, Any]) -> Any:
    from core.widgets.services.cpu.cpu_api import CpuData, CpuFreq

    freq = step.get("freq_mhz", [2400, 400, 3200])
    return CpuData(
        freq=CpuFreq(current=freq[0], min=freq[1], max=freq[2]),
        percent=step["percent"],
        percent_per_core=step.get("per_core", [step["percent"]]),
        cores_physical=step.get("cores_physical", 8),
        cores_logical=step.get("cores_logical", 16),
    )


def _cpu_build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.yasb.cpu import CpuWidget

    _reset_instances(CpuWidget)
    return CpuWidget(config)


def _cpu_step(widget: Any, step: dict[str, Any]) -> None:
    if step.get("toggle"):
        widget._toggle_label()
    else:
        widget._last_data = _cpu_data(step)
        widget._update_label(widget._last_data)


def _cpu_teardown(widget: Any) -> None:
    from core.widgets.yasb.cpu import CpuWidget

    _reset_instances(CpuWidget)


_CPU_PERCENTS = (0, 24.9, 25, 25.1, 36.5, 37.4, 37.5, 49.9, 50, 50.1, 89.9, 90, 90.5, 100)
register(
    LabelExporter(
        widget="cpu",
        widget_type="yasb.cpu.CpuWidget",
        build=_cpu_build,
        apply_step=_cpu_step,
        setup=_stub_worker("yasb.cpu.CpuWidget"),
        teardown_widget=_cpu_teardown,
        scenarios=[
            Scenario("placeholder_only"),
            Scenario(
                "user_options_values",
                steps=[{"percent": value} for value in _CPU_PERCENTS],
            ),
            Scenario(
                "hide_decimal_true",
                {"hide_decimal": True},
                steps=[{"percent": 37.4}, {"percent": 36.5}, {"percent": 37.5}],
            ),
            Scenario(
                "hide_decimal_false",
                {"hide_decimal": False},
                steps=[{"percent": 37.4}, {"percent": 100}],
            ),
            Scenario(
                "alt_label_toggle",
                {"hide_decimal": True},
                steps=[{"percent": 37.4}, {"toggle": True}, {"percent": 95}, {"toggle": True}],
            ),
            Scenario(
                "custom_labels_and_thresholds",
                {
                    "label": "<span>\uf2db</span> {info[percent]}%",
                    "label_alt": "{info[histograms][cpu_percent]} {info[freq][current]}",
                    "cpu_thresholds": {"low": 10, "medium": 20, "high": 30},
                    "hide_decimal": True,
                },
                steps=[{"percent": 5}, {"percent": 15}, {"percent": 25}, {"percent": 35}, {"toggle": True}],
            ),
        ],
    )
)


# --- memory ----------------------------------------------------------------


def _memory_data(step: dict[str, Any]) -> tuple[Any, Any]:
    from core.widgets.services.memory.memory_api import SwapMemory, VirtualMemory

    total = int(step["total_gb"] * GIB)
    used = int(step["used_gb"] * GIB)
    swap_total = int(step.get("swap_total_gb", 4) * GIB)
    swap_used = int(step.get("swap_used_gb", 1) * GIB)
    virtual = VirtualMemory(
        total=total,
        available=total - used,
        percent=step.get("percent", round(used / total * 100, 1) if total else 0.0),
        used=used,
        free=total - used,
    )
    swap = SwapMemory(
        total=swap_total,
        used=swap_used,
        free=swap_total - swap_used,
        percent=step.get("swap_percent", round(swap_used / swap_total * 100, 1) if swap_total else 0.0),
    )
    return virtual, swap


def _memory_build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.yasb.memory import MemoryWidget

    _reset_instances(MemoryWidget)
    return MemoryWidget(config)


def _memory_step(widget: Any, step: dict[str, Any]) -> None:
    if step.get("toggle"):
        widget._toggle_label()
    else:
        widget._update_label(*_memory_data(step))


def _memory_teardown(widget: Any) -> None:
    from core.widgets.yasb.memory import MemoryWidget

    _reset_instances(MemoryWidget)


_MEM_MAIN = {"total_gb": 31.6, "used_gb": 12.3}
register(
    LabelExporter(
        widget="memory",
        widget_type="yasb.memory.MemoryWidget",
        build=_memory_build,
        apply_step=_memory_step,
        setup=_stub_worker("yasb.memory.MemoryWidget"),
        teardown_widget=_memory_teardown,
        scenarios=[
            Scenario("placeholder_only"),
            Scenario(
                "user_options_12_3_of_31_6",
                steps=[
                    _MEM_MAIN,
                    {"toggle": True},
                    {"total_gb": 31.6, "used_gb": 30, "percent": 94.9},
                ],
            ),
            Scenario(
                "hide_decimal_false",
                {"hide_decimal": False, "label": "{virtual_mem_outof} {virtual_mem_percent}%"},
                steps=[_MEM_MAIN, {"total_gb": 16, "used_gb": 8, "percent": 50.0}],
            ),
            Scenario(
                "hide_decimal_true",
                {"hide_decimal": True, "label": "{virtual_mem_outof} {virtual_mem_percent}% {swap_mem_total}"},
                steps=[_MEM_MAIN, {"total_gb": 0.5, "used_gb": 0.25, "percent": 50.0}],
            ),
            Scenario(
                "all_placeholders",
                {
                    "hide_decimal": False,
                    "label": (
                        "{virtual_mem_free}|{virtual_mem_percent}|{virtual_mem_total}|{virtual_mem_avail}|"
                        "{virtual_mem_used}|{swap_mem_free}|{swap_mem_percent}|{swap_mem_total}|{histogram}"
                    ),
                },
                steps=[_MEM_MAIN],
            ),
        ],
    )
)


# --- battery ---------------------------------------------------------------


class _FakeBatteryApi:
    state: Any = None

    def get_status(self) -> Any:
        return type(self).state


def _battery_state(spec: dict[str, Any] | None) -> Any:
    if spec is None:
        return None
    from core.widgets.services.battery.battery_api import BatteryData

    fields: dict[str, Any] = {
        "percent": 50,
        "is_charging": False,
        "power_plugged": False,
        "time_remaining": 7380,
        "rate": 8.25,
        "voltage": 12.345,
        "capacity": 40000,
        "full_capacity": 50000,
        "designed_capacity": 55000,
        "temperature": 31.26,
        "cycle_count": 212,
        "chemistry": "LION",
        "health_percent": 90.9,
    }
    fields.update(spec)
    return BatteryData(**fields)


def _battery_setup() -> Any:
    from core.widgets.services.battery.battery_api import BatteryAPI

    original = BatteryAPI.__dict__["instance"]
    BatteryAPI.instance = classmethod(lambda cls: _FakeBatteryApi())

    def restore() -> None:
        BatteryAPI.instance = original
        _FakeBatteryApi.state = None

    return restore


def _battery_build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.yasb.battery import BatteryWidget

    _FakeBatteryApi.state = _battery_state(scenario.initial)
    widget = BatteryWidget(config)
    widget.timer.stop()
    widget._charging_blink_timer.stop()
    return widget


def _battery_step(widget: Any, step: dict[str, Any]) -> None:
    if step.get("toggle"):
        widget._toggle_label()
        return
    _FakeBatteryApi.state = _battery_state(step["battery"])
    widget._update_label()


def _battery_teardown(widget: Any) -> None:
    widget.timer.stop()
    widget._charging_blink_timer.stop()


def _bat(percent: int, **extra: Any) -> dict[str, Any]:
    return {"battery": {"percent": percent, **extra}}


register(
    LabelExporter(
        widget="battery",
        widget_type="yasb.battery.BatteryWidget",
        build=_battery_build,
        apply_step=_battery_step,
        setup=_battery_setup,
        after_show=lambda widget: widget._update_label(),
        teardown_widget=_battery_teardown,
        scenarios=[
            Scenario(
                "threshold_boundaries",
                initial={"percent": 50},
                steps=[_bat(p) for p in (0, 10, 11, 25, 26, 75, 76, 95, 96, 100)],
            ),
            Scenario(
                "charging",
                initial={"percent": 40, "is_charging": True, "power_plugged": True},
                steps=[
                    _bat(40, is_charging=True, power_plugged=True),
                    _bat(100, is_charging=True, power_plugged=True),
                ],
            ),
            Scenario(
                "plugged_not_charging",
                initial={"percent": 100, "power_plugged": True, "time_remaining": -2},
                steps=[_bat(100, power_plugged=True, time_remaining=-2)],
            ),
            Scenario(
                "time_remaining_natural",
                {"time_remaining_natural": True, "label": "{percent}% {time_remaining}"},
                initial={"percent": 60, "time_remaining": 7380},
                steps=[
                    _bat(60, time_remaining=7380),
                    _bat(60, time_remaining=45),
                    _bat(60, time_remaining=90),
                    _bat(60, time_remaining=86400 * 3),
                    _bat(60, time_remaining=-1),
                    _bat(60, time_remaining=-2),
                ],
            ),
            Scenario(
                "time_remaining_timedelta",
                {"time_remaining_natural": False, "label": "{percent}% {time_remaining}"},
                initial={"percent": 60, "time_remaining": 7380},
                steps=[_bat(60, time_remaining=7380), _bat(60, time_remaining=86400 * 3 + 5)],
            ),
            Scenario(
                "extended_fields",
                {
                    "label": "{power}|{voltage}|{capacity}|{full_capacity}|{designed_capacity}|"
                    "{temperature}|{cycle_count}|{health}|{chemistry}|{is_charging}"
                },
                initial={"percent": 50},
                steps=[
                    _bat(50),
                    _bat(
                        50,
                        rate=-3.14,
                        voltage=None,
                        capacity=None,
                        full_capacity=None,
                        designed_capacity=None,
                        temperature=None,
                        cycle_count=None,
                        chemistry=None,
                        health_percent=None,
                    ),
                ],
            ),
            Scenario(
                "no_battery_hide_unsupported",
                {"hide_unsupported": True},
                initial=None,
                steps=[{"battery": None}],
            ),
            Scenario(
                "no_battery_show_message",
                {"hide_unsupported": False},
                initial=None,
                steps=[{"battery": None}, _bat(50)],
            ),
            Scenario(
                "alt_label_toggle",
                initial={"percent": 80},
                steps=[{"toggle": True}, _bat(20), {"toggle": True}],
            ),
        ],
    )
)

import vec_github  # noqa: E402,F401  (registers the github label exporter)
import vec_komorebi  # noqa: E402,F401  (registers the komorebi exporters)

__all__ = ["LABEL_EXPORTERS", "export_label_vectors"]
