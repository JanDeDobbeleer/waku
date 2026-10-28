"""Label and maths vectors for the wallpapers widget (yasb.wallpapers.WallpapersWidget).

Imported by vec_labels.py. The widget is the real one, built offscreen; its label snapshot is the
usual one. Steps additionally run the pure parts of the wallpaper services and record the result
under `computed`, so the C++ port replays the same inputs:

  sort       collect_image_files over a scratch folder (Explorer name order, extension matching)
  builds     supported_types for Windows build numbers (webp gating)
  find       find_wallpapers (current wallpaper lookup, path normalisation)
  transcode  _transcode_size with given registry limits
  layout     WallpaperEngine._compute_per_screen_scaled for a fit mode and monitor layout
  reveal     the transition clip paths (bounding box and sample points)
  cards      gallery card placement for the four gallery types (visible cards, placements,
             fit_to_view, page_step, projected polygons)

Nothing here touches the real wallpaper, the desktop, the registry values the engine reads (they
are patched) or the user's folders.
"""

import os
import tempfile
from functools import partial
from types import SimpleNamespace
from typing import Any

from label_registry import LabelExporter, Scenario, register, snapshot

SORT_FILES = [
    "img10.png",
    "img2.png",
    "img1.png",
    "Img3.JPG",
    "notes.txt",
    "photo.jpeg",
    "a b.png",
    "z.BMP",
    "sub/img20.png",
    "sub/Img3.tif",
    "sub/deeper/last.gif",
    "xpng",
    "readme.md",
]

TRANSCODE_SIZES = [
    [1920, 1080],
    [3840, 2160],
    [5120, 2880],
    [3900, 3860],
    [7680, 4320],
    [8000, 8000],
    [10000, 1000],
    [1, 1],
]


def _image_modules() -> tuple[Any, Any, Any]:
    from core.widgets.services.wallpapers import engine, gallery, images

    return engine, gallery, images


def _build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.yasb.wallpapers import WallpapersWidget

    widget = WallpapersWidget(config)
    widget._parity = {}
    return widget


def _sort(spec: dict[str, Any]) -> dict[str, Any]:
    _, _, images = _image_modules()
    saved = images.supported_types
    images.supported_types = lambda: images.FILE_TYPES
    try:
        with tempfile.TemporaryDirectory() as folder:
            for name in spec["files"]:
                path = os.path.join(folder, *name.split("/"))
                os.makedirs(os.path.dirname(path), exist_ok=True)
                open(path, "wb").close()
            found = images.collect_image_files(folder)
            order = [os.path.relpath(path, folder).replace("\\", "/") for path in found]
    finally:
        images.supported_types = saved
    return {"order": order}


def _builds(spec: dict[str, Any]) -> dict[str, Any]:
    _, _, images = _image_modules()
    saved = images.get_build_and_ubr
    types = []
    try:
        for build, ubr in spec["builds"]:
            images.get_build_and_ubr = lambda build=build, ubr=ubr: (build, ubr)
            types.append(list(images.supported_types()))
    finally:
        images.get_build_and_ubr = saved
    return {"types": types}


def _find(spec: dict[str, Any]) -> dict[str, Any]:
    _, _, images = _image_modules()
    found = images.find_wallpapers(spec["files"], spec["wallpapers"])
    return {"found": found}


def _with_engine_settings(limits: Any, panorama: float) -> Any:
    engine_module, _, _ = _image_modules()
    saved = (engine_module._transcode_limits, engine_module._panorama_threshold)
    engine_module._transcode_limits = lambda: tuple(limits) if limits else None
    engine_module._panorama_threshold = lambda landscape: panorama

    def restore() -> None:
        engine_module._transcode_limits, engine_module._panorama_threshold = saved

    return restore


def _transcode(spec: dict[str, Any]) -> dict[str, Any]:
    engine_module, _, _ = _image_modules()
    restore = _with_engine_settings(spec.get("limits"), 2.22)
    try:
        sizes = [list(engine_module._transcode_size(w, h)) for w, h in spec["sizes"]]
    finally:
        restore()
    return {"sizes": sizes}


def _layout(spec: dict[str, Any]) -> dict[str, Any]:
    from PyQt6.QtGui import QColor, QPixmap

    engine_module, _, _ = _image_modules()
    restore = _with_engine_settings(spec.get("limits"), spec.get("panorama", 2.22))
    try:
        engine = engine_module.WallpaperEngine("unused.png", "circle")
        engine.fit_mode = spec["fit"]
        engine._bg_color = QColor(*spec.get("background", [0, 0, 0]))
        engine._pixmap_new = QPixmap(*spec["image"])
        engine._pixmap_old = QPixmap()
        dpr = spec.get("dpr", 1.0)
        engine.set_screen_areas([(x, y, w, h, dpr) for x, y, w, h in spec["areas"]])
        images = [[p.width(), p.height(), ox, oy] for p, ox, oy in engine._per_screen_scaled_new]
        engine.deleteLater()
    finally:
        restore()
    return {"images": images}


def _reveal(spec: dict[str, Any]) -> dict[str, Any]:
    from PyQt6.QtCore import QPointF

    engine_module, _, _ = _image_modules()
    engine = engine_module.WallpaperEngine("unused.png", spec["animation"])
    x, y, w, h = spec["rect"]
    frames = []
    for t in spec["progress"]:
        path = engine._clip_path_for_monitor(x, y, w, h, t)
        box = path.boundingRect()
        samples = [
            path.contains(QPointF(x + w * fx, y + h * fy))
            for fx, fy in ((0.5, 0.5), (0.05, 0.05), (0.95, 0.05), (0.05, 0.95), (0.95, 0.95), (0.5, 0.02))
        ]
        frames.append({"t": t, "box": [box.x(), box.y(), box.width(), box.height()], "samples": samples})
    engine.deleteLater()
    return {"frames": frames}


def _cards(spec: dict[str, Any]) -> dict[str, Any]:
    _, gallery, _ = _image_modules()
    cls = gallery.TYPES[spec["style"]]
    width = spec["width"]
    height = width * 9 // 16 if spec["orientation"] == "landscape" else width * 16 // 9
    count = spec["count"]
    view_w, view_h = spec["view"]

    card = SimpleNamespace(
        card_size=(width, height),
        image_files=[None] * count,
        neighbours=1,
        wraps=False,
        border=cls.border,
        dim=cls.dim,
    )
    for name in ("gap", "grow", "reach", "lean", "shrink", "fade"):
        if hasattr(cls, name):
            setattr(card, name, getattr(cls, name))
    step = cls.step.fget(card) if isinstance(cls.__dict__.get("step"), property) else cls.step
    card.step = step
    max_scale = cls.max_scale.fget(card) if isinstance(cls.__dict__.get("max_scale"), property) else cls.max_scale
    card.max_scale = max_scale
    view = SimpleNamespace(offset=0.0, index=0, count=count, gallery=card)
    view.width = lambda: view_w
    view.height = lambda: view_h
    view.relative_distance = partial(gallery.CardsView.relative_distance, view)
    view.visible_cards = partial(gallery.CardsView.visible_cards, view)
    view.project = partial(gallery.CardsView.project, view)
    card.view = view
    card.place = partial(cls.place, card)
    card.scale_at = partial(cls.scale_at, card) if hasattr(cls, "scale_at") else None
    card.selected_spot = partial(cls.selected_spot, card)

    gallery.Cards.fit_to_view(card, view_w, view_h)
    selected = card.selected_spot()
    area = view.project(selected, card.border).boundingRect()

    frames = []
    for offset in spec["offsets"]:
        view.offset = offset
        placed = cls.layout_cards(card)
        frames.append(
            {
                "offset": offset,
                "visible": [[index, distance] for index, distance in view.visible_cards()],
                "cards": [{"index": index, **spot._asdict()} for index, spot in placed],
                "normalised": gallery.Cards.normalise_offset(card, offset),
            }
        )
    polygon = view.project(selected)
    return {
        "neighbours": card.neighbours,
        "wraps": card.wraps,
        "max_scale": max_scale,
        "step": step,
        "page_step": gallery.Cards.page_step(card),
        "thumbnail": [area.width(), area.height()],
        "selected": selected._asdict(),
        "polygon": [[polygon.at(i).x(), polygon.at(i).y()] for i in range(polygon.count())],
        "frames": frames,
    }


_STEPS = {
    "sort": _sort,
    "builds": _builds,
    "find": _find,
    "transcode": _transcode,
    "layout": _layout,
    "reveal": _reveal,
    "cards": _cards,
}


def _apply(widget: Any, step: dict[str, Any]) -> None:
    widget._parity = {}
    for name, handler in _STEPS.items():
        if name in step:
            widget._parity = handler(step[name])
            return


def _snapshot(widget: Any) -> dict[str, Any]:
    return {**snapshot(widget), "computed": widget._parity}


def _fit_cases() -> list[dict[str, Any]]:
    one_screen = [[0, 0, 1920, 1080]]
    two_wide = [[0, 0, 1920, 1080], [1920, 0, 1920, 1080]]
    stacked = [[0, 0, 1920, 1080], [0, 1080, 1920, 1080]]
    staircase = [[0, 0, 1920, 1080], [1920, 40, 1920, 1080]]
    mixed = [[0, 0, 2560, 1440], [2560, 0, 1920, 1080]]
    cases: list[dict[str, Any]] = []
    limits = [7680, 3840]
    for fit in ("fill", "fit", "stretch", "center", "tile", "span"):
        cases.append(
            {
                "fit": fit,
                "image": [3000, 2000],
                "areas": one_screen,
                "limits": limits,
            }
        )
        cases.append({"fit": fit, "image": [1200, 1800], "areas": two_wide, "limits": limits})
    for fit in ("fill", "fit"):
        cases.append({"fit": fit, "image": [5760, 1080], "areas": two_wide, "limits": limits})
        cases.append({"fit": fit, "image": [5760, 1080], "areas": stacked, "limits": limits})
        cases.append({"fit": fit, "image": [5760, 1080], "areas": staircase, "limits": limits})
        cases.append({"fit": fit, "image": [5760, 1080], "areas": mixed, "limits": limits})
        cases.append({"fit": fit, "image": [5760, 1080], "areas": two_wide, "limits": limits, "panorama": 6.0})
        cases.append({"fit": fit, "image": [1080, 2400], "areas": two_wide, "limits": limits})
    cases.append({"fit": "fill", "image": [9000, 6000], "areas": one_screen, "limits": [3840, 2160]})
    cases.append({"fit": "fill", "image": [9000, 6000], "areas": one_screen, "limits": None})
    cases.append({"fit": "fill", "image": [3000, 2000], "areas": [[0, 0, 1920, 1080]], "dpr": 1.5, "limits": limits})
    cases.append(
        {"fit": "span", "image": [3000, 1000], "areas": [[0, 0, 1280, 720], [1280, 0, 1280, 720]], "dpr": 1.25, "limits": limits}
    )
    cases.append({"fit": "tile", "image": [640, 480], "areas": two_wide, "dpr": 1.5, "limits": limits})
    cases.append({"fit": "center", "image": [800, 600], "areas": one_screen, "dpr": 2.0, "limits": limits})
    return cases


def _card_cases() -> list[dict[str, Any]]:
    cases = []
    for style in ("default", "magnified", "strip", "slide"):
        for count, orientation, width, view in (
            (1, "landscape", 100, [1920, 80]),
            (2, "landscape", 100, [1920, 80]),
            (7, "portrait", 240, [1920, 435]),
            (40, "landscape", 160, [2560, 120]),
            (3, "portrait", 120, [800, 220]),
        ):
            cases.append(
                {
                    "style": style,
                    "width": width,
                    "orientation": orientation,
                    "count": count,
                    "view": view,
                    "offsets": [0.0, 0.4, 1.0, 2.5, count - 1.0, count - 0.6, count + 0.75, -0.3],
                }
            )
    return cases


_BASE: dict[str, Any] = {}

register(
    LabelExporter(
        widget="wallpapers",
        widget_type="yasb.wallpapers.WallpapersWidget",
        build=_build,
        apply_step=_apply,
        snapshot=_snapshot,
        include_normalized=True,
        scenarios=[
            Scenario("user_empty_label", {}, steps=[{}]),
            Scenario("default_label", {"label": "{icon}"}, steps=[{}]),
            Scenario("icon_and_text", {"label": "<span>\ue000</span> Wall"}, steps=[{}]),
            Scenario("gallery_options", {"gallery": {"type": "magnified", "image_width": 160, "accent_color": "auto"}}, steps=[{}]),
            Scenario("sort_order", {}, steps=[{"sort": {"files": SORT_FILES}}]),
            Scenario(
                "webp_gating",
                {},
                steps=[
                    {
                        "builds": {
                            "builds": [
                                [19045, 5000],
                                [22631, 9999],
                                [26100, 8036],
                                [26100, 8037],
                                [26200, 7000],
                                [26220, 7652],
                                [26220, 7653],
                                [26300, 0],
                            ]
                        }
                    }
                ],
            ),
            Scenario(
                "find_wallpapers",
                {},
                steps=[
                    {
                        "find": {
                            "files": ["C:\\Pics\\a.png", "C:\\Pics\\b.png", "C:\\Pics\\sub\\c.jpg"],
                            "wallpapers": {
                                "screen1": "c:/pics/B.PNG",
                                "screen2": "C:\\Pics\\sub\\..\\a.png",
                                "screen3": "C:\\Elsewhere\\x.png",
                                "screen4": "",
                                "screen5": "C:\\pics\\\\b.png",
                            },
                        }
                    }
                ],
            ),
            Scenario(
                "transcode_sizes",
                {},
                steps=[
                    {"transcode": {"limits": [7680, 3840], "sizes": TRANSCODE_SIZES}},
                    {"transcode": {"limits": [3840, 2160], "sizes": TRANSCODE_SIZES}},
                    {"transcode": {"limits": None, "sizes": TRANSCODE_SIZES}},
                ],
            ),
            Scenario("engine_layout", {}, steps=[{"layout": case} for case in _fit_cases()]),
            Scenario(
                "reveal_paths",
                {},
                steps=[
                    {
                        "reveal": {
                            "animation": animation,
                            "rect": [0, 0, 1920, 1080],
                            "progress": [0.0, 0.1, 0.5, 0.9, 1.0],
                        }
                    }
                    for animation in ("circle", "slide_top", "diamond", "split")
                ]
                + [{"reveal": {"animation": "split", "rect": [1920, 100, 1280, 720], "progress": [0.01, 0.4, 0.99]}}],
            ),
            Scenario("gallery_cards", {}, steps=[{"cards": case} for case in _card_cases()]),
        ],
    )
)
