"""Label vectors for the Wi-Fi and Bluetooth widgets.

Imported by vec_labels.py. The widget classes are the real ones, built offscreen; only the
service data (worker, menu, manager, host name lookup) is faked, so no radio or network is touched.
"""

from typing import Any

from label_registry import LabelExporter, Scenario, register

FAKE_IP = "192.0.2.10"


# --- wifi ------------------------------------------------------------------


class _FakeSocket:
    """Stands in for the socket module the widget uses for {ip_addr}."""

    address: str | None = FAKE_IP

    @staticmethod
    def gethostname() -> str:
        return "vector-host"

    @classmethod
    def gethostbyname(cls, _host: str) -> str:
        if cls.address is None:
            raise OSError("lookup failed")
        return cls.address


class _FakeWorker:
    last_result = None

    def __init__(self, *args: object):
        self.result = type("_Signal", (), {"connect": staticmethod(lambda slot: None)})()

    def start(self) -> None:
        pass


def _fake_menu(*args: object) -> Any:
    return type("_Menu", (), {"show_menu": staticmethod(lambda: None)})()


def _wifi_setup() -> Any:
    from core.widgets.yasb import wifi

    saved = (wifi.WiFiWorker, wifi.WifiMenu, wifi.socket)
    wifi.WiFiWorker = _FakeWorker
    wifi.WifiMenu = _fake_menu
    wifi.socket = _FakeSocket

    def restore() -> None:
        wifi.WiFiWorker, wifi.WifiMenu, wifi.socket = saved
        _FakeSocket.address = FAKE_IP

    return restore


def _wifi_build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.yasb.wifi import WifiWidget

    _FakeSocket.address = FAKE_IP
    return WifiWidget(config)


def _wifi_step(widget: Any, step: dict[str, Any]) -> None:
    if step.get("toggle"):
        widget._toggle_label()
        return
    from core.widgets.services.wifi.wifi_managers import WiFiInfo

    spec = step["wifi"]
    _FakeSocket.address = spec.get("ip", FAKE_IP)
    info = WiFiInfo(
        bars=spec.get("bars", 0),
        name=spec.get("name", "Disconnected"),
        exact_quality=spec.get("exact", -1),
        ethernet=spec.get("ethernet", False),
        vpn_name=spec.get("vpn", ""),
    )
    widget._on_wifi_info_result(info)


def _wifi(bars: int = 0, name: str = "Disconnected", **extra: Any) -> dict[str, Any]:
    return {"wifi": {"bars": bars, "name": name, **extra}}


_ALL_PLACEHOLDERS = "{wifi_icon}|{wifi_name}|{wifi_strength}|{ip_addr}|{vpn_icon}|{vpn_name}"

register(
    LabelExporter(
        widget="wifi",
        widget_type="yasb.wifi.WifiWidget",
        build=_wifi_build,
        apply_step=_wifi_step,
        setup=_wifi_setup,
        scenarios=[
            Scenario("placeholder_only"),
            Scenario(
                "user_options_connection_states",
                steps=[
                    _wifi(4, "Home"),
                    _wifi(3, "Home"),
                    _wifi(2, "Office"),
                    _wifi(1, "Cafe"),
                    _wifi(0),
                    _wifi(4, "Home", vpn="ProtonVPN"),
                    _wifi(4, "Home", ethernet=True),
                    _wifi(4, "Home", ethernet=True, vpn="ProtonVPN"),
                    _wifi(9, "Home"),
                    _wifi(4, "Home"),
                ],
            ),
            Scenario(
                "user_options_alt_label_toggle",
                steps=[
                    _wifi(4, "Home", vpn="ProtonVPN"),
                    {"toggle": True},
                    _wifi(2, "Office"),
                    _wifi(2, "Office", ethernet=True, vpn="ProtonVPN"),
                    _wifi(2, "Office", ethernet=True, ip="198.51.100.7"),
                    {"toggle": True},
                    _wifi(3, "Home"),
                ],
            ),
            Scenario(
                "all_placeholders",
                {
                    "label": _ALL_PLACEHOLDERS,
                    "label_alt": "<span>{wifi_icon}</span> {wifi_name} {wifi_strength}",
                    "ethernet_label": _ALL_PLACEHOLDERS,
                    "ethernet_label_alt": "<span>{wifi_icon}</span> {ip_addr}",
                },
                steps=[
                    _wifi(3, "Home", vpn="ProtonVPN"),
                    _wifi(3, "Home", ethernet=True, vpn="ProtonVPN"),
                    {"toggle": True},
                    _wifi(1, "Cafe"),
                    _wifi(0, ethernet=True),
                ],
            ),
            Scenario(
                "exact_strength_thresholds",
                {"get_exact_wifi_strength": True, "label": "{wifi_icon} {wifi_strength}"},
                steps=[_wifi(2, "Home", exact=q) for q in (-1, 0, 19, 20, 39, 40, 59, 60, 79, 80, 100)],
            ),
            Scenario(
                "hide_if_ethernet",
                {"hide_if_ethernet": True},
                steps=[
                    _wifi(3, "Home"),
                    _wifi(3, "Home", ethernet=True),
                    {"toggle": True},
                    _wifi(3, "Home"),
                    _wifi(3, "Home", ethernet=True, vpn="ProtonVPN"),
                    _wifi(2, "Home"),
                ],
            ),
            Scenario(
                "host_lookup_fails",
                {"label": "{wifi_icon} {wifi_name} {ip_addr}"},
                steps=[_wifi(3, "Home", ip=None), _wifi(3, "Home")],
            ),
            Scenario(
                "too_few_wifi_icons",
                {"wifi_icons": ["a", "b"], "label": "{wifi_icon} {wifi_name}"},
                steps=[_wifi(1, "Home"), _wifi(4, "Home"), _wifi(0, "Home")],
            ),
            Scenario(
                "custom_vpn_icon_and_classes",
                {
                    "vpn_icon": "V",
                    "class_name": "extra",
                    "label": '<span class="net">{wifi_icon}</span> <span class="vpn">{vpn_icon}</span> {vpn_name}',
                },
                steps=[_wifi(4, "Home"), _wifi(4, "Home", vpn="Work VPN")],
            ),
        ],
    )
)


# --- bluetooth -------------------------------------------------------------


class _FakeSignal:
    def connect(self, slot: object) -> None:
        pass

    def disconnect(self, slot: object = None) -> None:
        pass


class _FakeManager:
    status_updated = _FakeSignal()
    refresh_failed = _FakeSignal()
    scan_started = _FakeSignal()
    scan_completed = _FakeSignal()
    connection_finished = _FakeSignal()

    @classmethod
    def acquire(cls) -> "_FakeManager":
        return cls()

    def start(self) -> None:
        pass

    def release(self) -> None:
        pass

    def is_radio_on(self) -> bool:
        return False

    def devices(self) -> list[Any]:
        return []


def _bluetooth_setup() -> Any:
    from core.widgets.yasb import bluetooth

    saved = bluetooth.BluetoothManager
    bluetooth.BluetoothManager = _FakeManager

    def restore() -> None:
        bluetooth.BluetoothManager = saved

    return restore


def _bluetooth_build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.yasb.bluetooth import BluetoothWidget

    return BluetoothWidget(config)


def _bluetooth_step(widget: Any, step: dict[str, Any]) -> None:
    if step.get("toggle"):
        widget._toggle_label()
        return
    from core.widgets.services.bluetooth.bluetooth_types import BluetoothStatus, DeviceInfo

    spec = step["status"]
    devices = [
        DeviceInfo(
            name=item["name"],
            address=item.get("address", f"AA:BB:CC:00:00:{index:02X}"),
            connected=item.get("connected", False),
            paired=item.get("paired", True),
            remembered=item.get("remembered", False),
        )
        for index, item in enumerate(spec.get("devices", []))
    ]
    widget._on_status_updated(BluetoothStatus(radio_on=spec["radio_on"], devices=devices))


def _bt(radio_on: bool, *devices: dict[str, Any]) -> dict[str, Any]:
    return {"status": {"radio_on": radio_on, "devices": list(devices)}}


def _dev(name: str, connected: bool = True, **extra: Any) -> dict[str, Any]:
    return {"name": name, "connected": connected, **extra}


register(
    LabelExporter(
        widget="bluetooth",
        widget_type="yasb.bluetooth.BluetoothWidget",
        build=_bluetooth_build,
        apply_step=_bluetooth_step,
        setup=_bluetooth_setup,
        scenarios=[
            Scenario("placeholder_only"),
            Scenario(
                "user_options_states",
                steps=[
                    _bt(False),
                    _bt(True),
                    _bt(True, _dev("Headphones")),
                    _bt(True, _dev("Headphones"), _dev("Keyboard")),
                    _bt(True, _dev("Headphones", connected=False)),
                    _bt(True, _dev("Unpaired Mouse", paired=False, remembered=False)),
                    _bt(True, _dev("Remembered Pad", paired=False, remembered=True)),
                    _bt(False, _dev("Headphones")),
                ],
            ),
            Scenario(
                "user_options_alt_label_toggle",
                steps=[
                    _bt(True, _dev("Headphones")),
                    {"toggle": True},
                    _bt(True, _dev("Headphones"), _dev("Keyboard")),
                    _bt(False),
                    {"toggle": True},
                    _bt(True),
                ],
            ),
            Scenario(
                "max_length_truncation",
                {"label_alt": "<span>{icon}</span> {device_name}", "max_length": 10},
                steps=[
                    {"toggle": True},
                    _bt(True, _dev("Short")),
                    _bt(True, _dev("Exactly 10")),
                    _bt(True, _dev("Eleven char")),
                    _bt(True, _dev("A very long headset name")),
                    _bt(True, _dev("\U0001f3a7\U0001f3a7\U0001f3a7\U0001f3a7\U0001f3a7\U0001f3a7")),
                ],
            ),
            Scenario(
                "max_length_unset_and_ellipsis",
                {
                    "label": "{device_name}|{device_count}",
                    "label_alt": "<span>{icon}</span>{device_name}",
                    "max_length": None,
                    "max_length_ellipsis": "~",
                },
                steps=[_bt(True, _dev("A very long headset name that is not truncated")), {"toggle": True}],
            ),
            Scenario(
                "aliases_separator_and_no_device_label",
                {
                    "label": "<span>{icon}</span> {device_name} ({device_count})",
                    "label_no_device": "none",
                    "label_device_separator": " + ",
                    "device_aliases": [{"name": "WH-1000XM5", "alias": "Sony"}, {"name": " Keys ", "alias": "K"}],
                    "icons": {"bluetooth_on": "ON", "bluetooth_off": "OFF", "bluetooth_connected": "CONN"},
                },
                steps=[
                    _bt(True),
                    _bt(True, _dev("WH-1000XM5"), _dev("Keys"), _dev("Other")),
                    _bt(False),
                ],
            ),
            Scenario(
                "tooltip_disabled_and_class_name",
                {"tooltip": False, "class_name": "extra", "label": "<span class=\"glyph\">{icon}</span> {device_count}"},
                steps=[_bt(True, _dev("Headphones")), _bt(True)],
            ),
        ],
    )
)
