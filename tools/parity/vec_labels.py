"""Label vectors for the widgets ported so far: cpu, memory, battery, clock.

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


# --- clock -----------------------------------------------------------------

_CLOCK: dict[str, Any] = {"wall": None}
_CLOCK_DEFAULT_TIME = "2026-10-28T23:59:05.123456"


def _clock_setup() -> Any:
    from datetime import datetime as real_datetime
    from datetime import timezone

    import core.widgets.yasb.clock as clock_module

    original = clock_module.datetime

    class FixedDatetime(real_datetime):
        @classmethod
        def now(cls, tz: Any = None) -> Any:
            wall = _CLOCK["wall"]
            if tz is None:
                # Local wall clock: naive, so astimezone() keeps the wall time.
                return cls(wall.year, wall.month, wall.day, wall.hour, wall.minute, wall.second, wall.microsecond)
            # Named zones read the same fields as a UTC instant.
            instant = real_datetime(
                wall.year,
                wall.month,
                wall.day,
                wall.hour,
                wall.minute,
                wall.second,
                wall.microsecond,
                tzinfo=timezone.utc,
            )
            return instant.astimezone(tz)

    clock_module.datetime = FixedDatetime

    def restore() -> None:
        clock_module.datetime = original
        clock_module.ClockWidgetSharedState._instance = None

    return restore


def _clock_build(config: Any, scenario: Scenario) -> Any:
    from datetime import datetime as real_datetime

    import core.widgets.yasb.clock as clock_module

    clock_module.ClockWidgetSharedState._instance = None
    _CLOCK["wall"] = real_datetime.fromisoformat((scenario.initial or {}).get("time", _CLOCK_DEFAULT_TIME))
    widget = clock_module.ClockWidget(config)
    widget.timer.stop()
    return widget


def _clock_step(widget: Any, step: dict[str, Any]) -> None:
    import copy
    from datetime import datetime as real_datetime

    state = widget._shared_state
    if "time" in step:
        _CLOCK["wall"] = real_datetime.fromisoformat(step["time"])
    if step.get("toggle"):
        widget._toggle_label()
        return
    if step.get("next_timezone"):
        widget._next_timezone()
        return
    if "alarms" in step:
        state.alarms = copy.deepcopy(step["alarms"])
    if "snoozed" in step:
        state.snoozed_alarms = [
            {"alarm": copy.deepcopy(alarm), "snooze_until": real_datetime(2099, 1, 1)} for alarm in step["snoozed"]
        ]
    if "timer" in step:
        state.timer_active = step["timer"] is not None
        state.timer_seconds_remaining = step["timer"] or 0
    widget.update_label()


def _clock_snapshot(widget: Any) -> dict[str, Any]:
    from label_registry import snapshot as base_snapshot

    snap = base_snapshot(widget)
    snap["timer"] = _label_state_of(widget._timer_label)
    return snap


def _label_state_of(label: Any) -> dict[str, Any]:
    return {"text": label.text(), "class": label.property("class"), "hidden": label.isHidden()}


def _clock_teardown(widget: Any) -> None:
    widget.timer.stop()


_CLOCK_ALARM = {"time": "07:00", "days": [0, 1], "enabled": True, "title": "Wake up"}
_CLOCK_FORMATS = (
    "{%A %a %B %b %d %e %H %I %M %S %p %j %y %Y %m}",
    "{%U %W %w %u %V %G %g %C}",
    "{%D|%F|%T|%R|%r|%c|%x|%X|%%|%h|%n|%t}",
    "{%#d %#H %#I %#j %#m %#M %#S %#y %#p}",
    "{%Ey %Od %EY %Ec %Ex %EX %OH %Oe}",
    "{%f}",
    "{%z|%:z|%Z}",
    "{%-d}",
    "{%}",
    "{%Q}",
    "{%k %l %s %P}",
    "{%E}",
    "{%:}",
    "{%d %-m}",
    "{%d} and {%m}",
    "{{%d}}",
    "{}",
    "{ }",
    "no braces %d",
    "a {%H} b <span>{%M}</span> c {%S}",
    "<span>{%H:%M}</span>",
    "{%H:%M}<span>{icon}</span>",
    "{alarm} {%H:%M}",
    "{icon}{%H}{alarm}",
    "{%Y\n%m}",
    "{%y%%%d}",
    "{%A, %B %d, %Y}é",
)

register(
    LabelExporter(
        widget="clock",
        widget_type="yasb.clock.ClockWidget",
        build=_clock_build,
        apply_step=_clock_step,
        setup=_clock_setup,
        teardown_widget=_clock_teardown,
        snapshot=_clock_snapshot,
        scenarios=[
            Scenario(
                "user_options",
                steps=[
                    {"time": "2026-10-28T23:59:59.999999"},
                    {"time": "2026-10-29T00:00:00.000000"},
                    {"toggle": True},
                    {"time": "2026-10-29T00:01:00.500000"},
                    {"toggle": True},
                ],
            ),
            Scenario(
                "timer_label",
                steps=[
                    {"timer": 65},
                    {"timer": 3725},
                    {"toggle": True},
                    {"timer": 59},
                    {"timer": 0},
                    {"timer": None},
                    {"toggle": True},
                    {"timer": 36000},
                ],
            ),
            Scenario(
                "timezone_abbreviations",
                {
                    "label": "{%H:%M %Z}",
                    "label_alt": "{%Z %z}",
                    "timezones": [
                        "Asia/Kolkata",
                        "America/New_York",
                        "Europe/London",
                        "Europe/Paris",
                        "Australia/Sydney",
                        "Asia/Tokyo",
                        "Asia/Dubai",
                        "UTC",
                    ],
                },
                steps=[{"next_timezone": True}] * 8
                + [{"time": "2026-01-15T12:00:00"}]
                + [{"next_timezone": True}] * 8,
            ),
            Scenario(
                "alarm_states",
                steps=[
                    {"alarms": [_CLOCK_ALARM]},
                    {"alarms": [{**_CLOCK_ALARM, "enabled": False}]},
                    {"alarms": [{"time": "08:00"}]},
                    {"alarms": [{**_CLOCK_ALARM, "enabled": False}], "snoozed": [_CLOCK_ALARM]},
                    {"alarms": [], "snoozed": []},
                    {"alarms": [_CLOCK_ALARM]},
                    {"toggle": True},
                    {"toggle": True},
                ],
            ),
            Scenario(
                "alarm_in_plain_text",
                {"label": "{alarm} {%H:%M}", "label_alt": "<span>{alarm}</span> {alarm}"},
                steps=[
                    {"alarms": [_CLOCK_ALARM]},
                    {"snoozed": [_CLOCK_ALARM]},
                    {"alarms": [], "snoozed": []},
                    {"toggle": True},
                    {"alarms": [_CLOCK_ALARM]},
                ],
            ),
            Scenario(
                "alarm_icons_custom",
                {
                    "label": "<span>{alarm}</span> {%H}",
                    "alarm_icons": {"enabled": "ON", "disabled": "OFF", "snooze": "ZZ"},
                },
                steps=[{"alarms": [_CLOCK_ALARM]}, {"snoozed": [_CLOCK_ALARM]}],
            ),
            Scenario(
                "icons_by_hour",
                {
                    "label": "<span>{icon}</span> {%H:%M}",
                    "label_alt": "<span>{icon}</span>",
                    "icons": {"clock_00": "A", "clock_01": "B", "clock_11": "C", "clock_12": "D"},
                },
                steps=[{"time": f"2026-10-28T{hour:02d}:30:00"} for hour in (0, 1, 2, 11, 12, 13, 14, 23)]
                + [{"toggle": True}, {"time": "2026-10-28T13:30:00"}],
            ),
            Scenario(
                "timezones_cycle",
                {
                    "label": "{%H:%M %z %:z}",
                    "label_alt": "{%A}",
                    "timezones": ["UTC", "Asia/Kolkata", "America/New_York"],
                },
                steps=[
                    {"next_timezone": True},
                    {"next_timezone": True},
                    {"next_timezone": True},
                    {"toggle": True},
                    {"next_timezone": True},
                ],
            ),
            Scenario(
                "timezones_invalid_ignored",
                {"label": "{%H:%M %z}", "timezones": ["Not/AZone", "UTC"]},
                steps=[{"next_timezone": True}],
            ),
            Scenario(
                "timezones_all_invalid",
                {"label": "{%H:%M}", "timezones": ["Not/AZone"]},
                steps=[{"next_timezone": True}],
            ),
            Scenario(
                "locale_dutch",
                {"locale": "nl-BE", "label": "{%A %d %B %a %b %p}", "label_alt": "{%c|%x|%X}"},
                steps=[{"toggle": True}, {"time": "2026-03-05T03:09:05"}, {"toggle": True}],
            ),
            Scenario(
                "locale_invalid_falls_back",
                {"locale": "zz-nope", "label": "{%A %B %c}"},
                steps=[{"time": "2026-03-05T03:09:05"}],
            ),
        ]
        + [
            Scenario(
                f"format_{index:02d}",
                {"label": fmt, "label_alt": fmt, "timezones": ["UTC"]},
                steps=[{"toggle": True}, {"time": "2026-01-02T03:04:05.000007"}, {"toggle": True}],
            )
            for index, fmt in enumerate(_CLOCK_FORMATS)
        ],
    )
)

import vec_control_center  # noqa: E402,F401  (registers the control center exporter)
import vec_github  # noqa: E402,F401  (registers the github label exporter)
import vec_glazewm_workspaces  # noqa: E402,F401  (registers the glazewm workspaces exporter)
import vec_komorebi  # noqa: E402,F401  (registers the komorebi exporters)
import vec_media  # noqa: E402,F401  (registers the media exporter)
import vec_notifications  # noqa: E402,F401  (registers the notifications exporter)
import vec_labels_network  # noqa: E402,F401  (registers the wifi and bluetooth exporters)
import vec_wallpapers  # noqa: E402,F401  (registers the wallpapers exporter)
import vec_systray  # noqa: E402,F401  (registers the systray exporter)

__all__ = ["LABEL_EXPORTERS", "export_label_vectors"]
