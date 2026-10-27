"""Adaptive bar island geometry vectors, one per scenario of tests/bar/test_adaptive_bar.py."""

import itertools
from typing import Any

BAR_HEIGHT = 30
RAIL = 4
EDGE_RADIUS = 10
EXCLUDED = "excluded"
LAYOUTS: dict[str, list[tuple[str, int, str | None]]] = {
    "edges_and_centre": [("left", 100, None), ("center", 120, None), ("right", 80, None)],
    "centre_only": [("center", 120, None)],
    "empty": [],
    "excluded_at_edge": [("left", 60, EXCLUDED), ("left", 100, None), ("center", 120, None)],
}


class _FakeAppBar:
    def create_appbar(self, *args: Any, **kwargs: Any) -> None:
        pass

    def remove_appbar(self) -> None:
        pass


def _wait(ms: int) -> None:
    from PyQt6.QtCore import QEventLoop, QTimer

    loop = QEventLoop()
    QTimer.singleShot(ms, loop.quit)
    loop.exec()


def _path_elements(path: Any) -> list[list[float]]:
    return [
        [path.elementAt(i).type.value, round(path.elementAt(i).x, 3), round(path.elementAt(i).y, 3)]
        for i in range(path.elementCount())
    ]


def _rect(rect: Any) -> list[int]:
    return [rect.x(), rect.y(), rect.width(), rect.height()]


def _make_widget(width: int, name: str | None) -> Any:
    from core.widgets.base import BaseWidget

    widget = BaseWidget()
    widget.setFixedWidth(width)
    widget.widget_name = name
    return widget


def _make_bar(
    css: str,
    layout: list[tuple[str, int, str | None]],
    rules: str = "",
    groups: dict[str, list[Any]] | None = None,
    **config: Any,
) -> Any:
    from PyQt6.QtWidgets import QApplication

    from core.bar import Bar
    from core.validation.bar import BarConfig

    if groups is None:
        groups = {"left": [], "center": [], "right": []}
        for group, width, name in layout:
            groups[group].append(_make_widget(width, name))
    else:
        groups = {"left": [], "center": [], "right": [], **groups}
    screen = QApplication.primaryScreen()
    bar = Bar(
        bar_id="adaptive",
        bar_name="adaptive",
        bar_screen=screen,
        stylesheet=f".bar {{ background: rgb(200, 0, 0); qproperty-railheight: {RAIL}; {css} }} {rules}",
        widgets=groups,
        config=BarConfig.model_validate(
            {
                "style": "adaptive",
                "style_adaptive_exclude": [EXCLUDED],
                "animation": {"enabled": False},
                "dimensions": {"height": BAR_HEIGHT},
                **config,
            }
        ),
        init=True,
    )
    _wait(20)
    return bar


def _snapshot(bar: Any) -> dict[str, Any]:
    from PyQt6.QtCore import QPoint

    from core.widgets.base import BaseWidget

    frame = bar.bar_frame
    widgets: list[dict[str, Any]] = []
    layout = frame.layout()
    for index in range(layout.count()):
        container = layout.itemAt(index).widget()
        if container is None:
            continue
        for child in container.findChildren(BaseWidget):
            origin = child.mapTo(frame, QPoint(0, 0))
            widgets.append(
                {
                    "container": container.property("class"),
                    "name": child.widget_name,
                    "rect": [origin.x(), origin.y(), child.width(), child.height()],
                }
            )
    color = frame.bordercolor
    return {
        "islands": [list(span) for span in frame._islands],
        "edge_overhang": frame.edge_overhang,
        "frame_size": [frame.width(), frame.height()],
        "bar_geometry": _rect(bar.geometry()),
        "click_rect": _rect(frame.click_rect()),
        "properties": {
            "railheight": frame.railheight,
            "islandradius": frame.islandradius,
            "grouppadding": frame.grouppadding,
            "islands": frame.islands,
            "edgeradius": frame.edgeradius,
            "borderwidth": frame.borderwidth,
            "bordercolor": [color.red(), color.green(), color.blue(), color.alpha()],
        },
        "widgets": widgets,
        "shape": _path_elements(frame._shape),
        "border_path": _path_elements(frame._border_path),
    }


def _close(bar: Any) -> None:
    bar.skip_animation = True
    bar.close()


def _case(css: str, layout: list[tuple[str, int, str | None]], rules: str = "", **config: Any) -> dict[str, Any]:
    bar = _make_bar(css, layout, rules, **config)
    try:
        return _snapshot(bar)
    finally:
        _close(bar)


def _inputs(css: str, layout: list[tuple[str, int, str | None]], rules: str = "", **config: Any) -> dict[str, Any]:
    return {
        "bar_css": css,
        "extra_rules": rules,
        "layout": [{"group": group, "width": width, "name": name} for group, width, name in layout],
        "bar_config": config,
        "bar_height": BAR_HEIGHT,
        "excluded_names": [EXCLUDED],
    }


def export_islands() -> dict[str, Any]:
    from PyQt6.QtWidgets import QApplication

    from core.utils.win32 import app_bar

    app_bar.Win32AppBar = _FakeAppBar  # type: ignore[misc]
    screen = QApplication.primaryScreen().geometry()
    cases: dict[str, Any] = {}

    def add(name: str, css: str, layout: list[tuple[str, int, str | None]], rules: str = "", **config: Any) -> None:
        cases[name] = {"inputs": _inputs(css, layout, rules, **config), "expected": _case(css, layout, rules, **config)}

    for position, islands, edge_radius, border, layout in itertools.product(
        ("top", "bottom"), (True, False), (0, EDGE_RADIUS), (False, True), LAYOUTS
    ):
        css = f"qproperty-edgeradius: {edge_radius}; qproperty-islands: {'true' if islands else 'false'};"
        if border:
            css += " qproperty-borderwidth: 2; qproperty-bordercolor: rgb(0, 0, 255);"
        name = "-".join(
            (
                "combination",
                position,
                "islands" if islands else "no_islands",
                f"edgeradius{edge_radius}",
                "border" if border else "no_border",
                layout,
            )
        )
        add(name, css, LAYOUTS[layout], alignment={"position": position})

    add("excluded_widget_splits_group", "", [("center", 100, None), ("center", 80, EXCLUDED), ("center", 100, None)])
    add(
        "island_near_screen_edge_snaps",
        "",
        [("left", 100, None)],
        rules=".container-left { padding-left: 12px; }",
    )
    for label, dimensions in (("auto_width", {"width": "auto"}), ("fixed_width", {"width": 600})):
        add(
            f"edge_curves_need_full_width_{label}",
            f"qproperty-edgeradius: {EDGE_RADIUS};",
            [("center", 120, None)],
            dimensions={**dimensions, "height": BAR_HEIGHT},
        )

    cases["island_grows_with_widget"] = _grow_case()
    for position in ("top", "bottom"):
        cases[f"edgeradius_change_keeps_edge_{position}"] = _edge_change_case(position)

    return {"screen_geometry": _rect(screen), "bar_height": BAR_HEIGHT, "cases": cases}


def _grow_case() -> dict[str, Any]:
    from PyQt6.QtWidgets import QLabel

    from core.widgets.base import BaseWidget

    clock = BaseWidget()
    label = QLabel("12:00")
    label.setFixedWidth(60)
    clock.widget_layout.addWidget(label)
    bar = _make_bar("", [], groups={"center": [clock]})
    try:
        before = [list(span) for span in bar.bar_frame._islands]
        label.setFixedWidth(220)
        _wait(20)
        after = [list(span) for span in bar.bar_frame._islands]
        return {
            "inputs": {"group": "center", "label_width_before": 60, "label_width_after": 220},
            "expected": {"islands_before": before, "islands_after": after},
        }
    finally:
        _close(bar)


def _edge_change_case(position: str) -> dict[str, Any]:
    bar = _make_bar("qproperty-edgeradius: 0;", [("center", 120, None)], alignment={"position": position})
    try:
        screen = bar.target_screen.geometry()
        before = bar.geometry()
        bar.setStyleSheet(
            f".bar {{ background: rgb(200, 0, 0); qproperty-railheight: {RAIL}; qproperty-edgeradius: {EDGE_RADIUS}; }}"
        )
        immediately = bar.geometry()
        _wait(20)
        after = bar.geometry()
        return {
            "inputs": {"position": position, "edge_radius_before": 0, "edge_radius_after": EDGE_RADIUS},
            "expected": {
                "screen_geometry": _rect(screen),
                "padding": dict(bar.padding),
                "geometry_before": _rect(before),
                "geometry_immediately_after_stylesheet": _rect(immediately),
                "geometry_after": _rect(after),
                "bar_height_after": bar.height(),
            },
        }
    finally:
        _close(bar)
