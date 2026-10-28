"""Label vectors for the media widget (yasb.media.MediaWidget).

The real widget is built offscreen with a fake `WindowsMedia` service: scenarios feed it sessions
(title, artist, playback flags, timeline) and the vectors record the label, button, progress bar
and flyout state the widget renders. Thumbnails are synthetic PNG-free PIL images; only whether the
thumbnail label shows a pixmap is recorded, never its pixel size (font metrics differ per build).
"""

from typing import Any

from label_registry import LabelExporter, Scenario, register

APP_ID = "Fake.Player.App"
SOURCE_NAMES = {APP_ID: "Fake Player", "Other.Player.App": "Other Player"}
BASE_SESSION: dict[str, Any] = {
    "app_id": APP_ID,
    "title": "Song",
    "artist": "Artist",
    "playing": True,
    "prev_enabled": True,
    "play_enabled": True,
    "next_enabled": True,
    "timeline_enabled": False,
    "duration": 0.0,
    "position": 0.0,
    "thumbnail": False,
}


def _fake_media_class() -> Any:
    from PyQt6.QtCore import QObject, pyqtSignal

    class FakeMedia(QObject):
        media_data_changed = pyqtSignal(dict)
        current_session_changed = pyqtSignal()
        media_properties_changed = pyqtSignal()
        timeline_info_changed = pyqtSignal()
        playback_info_changed = pyqtSignal()

        def __init__(self) -> None:
            super().__init__()
            self.trackers: dict[str, Any] = {}

        def force_update(self) -> None:
            self.media_properties_changed.emit()
            self.media_data_changed.emit(self.trackers)

        def play_pause(self) -> None:
            pass

        def prev(self) -> None:
            pass

        def next(self) -> None:
            pass

        def switch_current_session(self, direction: int) -> None:
            pass

    return FakeMedia


def _setup() -> Any:
    import core.widgets.yasb.media as module

    original_media = module.WindowsMedia
    original_resolve = module.resolve_source_app_name
    module.WindowsMedia = _fake_media_class()
    module.resolve_source_app_name = lambda aumid: SOURCE_NAMES.get(aumid)

    def restore() -> None:
        module.WindowsMedia = original_media
        module.resolve_source_app_name = original_resolve

    return restore


def _state(spec: dict[str, Any]) -> Any:
    from PIL import Image

    from core.widgets.services.media.media import SessionState

    full = {**BASE_SESSION, **spec}
    state = SessionState(full["app_id"])
    state.title = full["title"]
    state.artist = full["artist"]
    state.is_current = True
    state.is_playing = full["playing"]
    state.playback_status = 4 if full["playing"] else 5
    state.playback_ready = True
    state.controls_prev_enabled = full["prev_enabled"]
    state.controls_play_enabled = full["play_enabled"]
    state.controls_next_enabled = full["next_enabled"]
    state.timeline_enabled = full["timeline_enabled"]
    state.duration = full["duration"]
    state.current_pos = full["position"]
    state.last_snapshot_pos = full["position"]
    if full["thumbnail"]:
        state.thumbnail = Image.new("RGB", (40, 20), (200, 80, 40))
    return state


def _build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.yasb.media import MediaWidget

    return MediaWidget(config)


def _apply(widget: Any, step: dict[str, Any]) -> None:
    media = widget.media
    if "session" in step:
        spec = step["session"]
        media.trackers = {} if spec is None else {spec.get("app_id", APP_ID): _state(spec)}
        media.media_data_changed.emit(media.trackers)
        media.current_session_changed.emit()
        if spec is not None:
            media.media_properties_changed.emit()
            media.playback_info_changed.emit()
            media.timeline_info_changed.emit()
    elif step.get("toggle"):
        widget._toggle_label()
    elif step.get("menu"):
        widget.show_menu()
    elif step.get("close_menu"):
        widget.dialog.hide()


def _label(label: Any) -> dict[str, Any] | None:
    if label is None:
        return None
    return {"text": label.text(), "class": label.property("class"), "hidden": label.isHidden()}


def _menu(widget: Any) -> dict[str, Any] | None:
    dialog = getattr(widget, "dialog", None)
    if dialog is None or not dialog.isVisible():
        return None
    from PyQt6.QtWidgets import QLabel

    def child(name: str) -> Any:
        try:
            return getattr(widget, name, None)
        except RuntimeError:
            return None

    labels = {
        "title": child("_popup_title_label"),
        "artist": child("_popup_artist_label"),
        "source": child("_popup_source_label"),
        "current_time": child("_popup_current_time_label"),
        "total_time": child("_popup_total_time_label"),
        "prev": child("_popup_prev_label"),
        "play": child("_popup_play_button"),
        "next": child("_popup_next_label"),
    }
    texts = [label.text() for label in dialog.findChildren(QLabel) if label.property("class") == "no-media"]
    result: dict[str, Any] = {
        "class": dialog.property("class"),
        "no_media": texts[0] if texts else None,
    }
    for key, label in labels.items():
        try:
            result[key] = _label(label) if label is not None else None
        except RuntimeError:
            result[key] = None
    container = getattr(widget, "_time_slider_container", None)
    result["timeline_hidden"] = container.isHidden() if container is not None else None
    result["slider_value"] = widget._progress_slider.value() if getattr(widget, "_progress_slider", None) else None
    thumb = getattr(widget, "_popup_thumbnail_label", None)
    result["thumbnail_has_pixmap"] = (not thumb.pixmap().isNull()) if thumb is not None else None
    return result


def _snapshot(widget: Any) -> dict[str, Any]:
    layout = widget._widget_container_layout
    order: list[str] = []
    buttons = {id(widget._prev_label): "prev", id(widget._play_label): "play", id(widget._next_label): "next"}
    for index in range(layout.count()):
        item = layout.itemAt(index)
        child = item.widget()
        order.append(buttons.get(id(child), "other") if child is not None else "stack")
    return {
        "widget_hidden": widget.isHidden(),
        "frame_class": widget._widget_frame.property("class"),
        "container_class": widget._widget_container.property("class"),
        "label": _label(widget._label),
        "label_alt": _label(widget._label_alt),
        "thumbnail_hidden": widget._thumbnail_label.isHidden(),
        "thumbnail_has_pixmap": not widget._thumbnail_label.pixmap().isNull(),
        "progress_hidden": widget._progress_bar.isHidden(),
        "progress_value": widget._progress_bar.value(),
        "progress_container_hidden": widget._progress_bar.parentWidget().isHidden(),
        "prev": _label(widget._prev_label),
        "play": _label(widget._play_label),
        "next": _label(widget._next_label),
        "layout_order": order,
        "menu": _menu(widget),
    }


def _s(**fields: Any) -> dict[str, Any]:
    return {"session": fields}


LONG_TITLE = "A very long song title that keeps going and going"
LONG_ARTIST = "Some Artist With A Long Name"
NO_BLUR = {"media_menu": {"blur": False}}

register(
    LabelExporter(
        widget="media",
        widget_type="yasb.media.MediaWidget",
        build=_build,
        apply_step=_apply,
        setup=_setup,
        snapshot=_snapshot,
        scenarios=[
            Scenario(
                "user_label_truncation",
                steps=[
                    _s(),
                    _s(title=LONG_TITLE, artist=LONG_ARTIST),
                    _s(title="Exactly thirty characters long!", artist=""),
                    _s(title="", artist="Only Artist"),
                    _s(title="", artist=""),
                    _s(title="日本語のタイトル😀😀 and some more text to cut", artist="Ünïcode Ärtist"),
                    {"toggle": True},
                    _s(title=LONG_TITLE, artist=LONG_ARTIST),
                    {"toggle": True},
                    {"session": None},
                ],
            ),
            Scenario(
                "per_field_truncation",
                {
                    "label": "{title}{s}{artist}",
                    "label_alt": "[{artist}] {title}",
                    "separator": " | ",
                    "max_field_size": {"label": 10, "label_alt": 12, "truncate_whole_label": False},
                },
                steps=[
                    _s(title=LONG_TITLE, artist=LONG_ARTIST),
                    _s(title="Short", artist=""),
                    _s(title="", artist="Short"),
                    {"toggle": True},
                    _s(title=LONG_TITLE, artist=LONG_ARTIST),
                ],
            ),
            Scenario(
                "tiny_max_field_size",
                {"max_field_size": {"label": 2, "label_alt": 3, "truncate_whole_label": True}},
                steps=[_s(title="abcdef", artist="xyz"), {"toggle": True}, _s(title="abcdef", artist="xyz")],
            ),
            Scenario(
                "label_placeholders",
                {
                    "label": "{unknown}{s}{title}{s}{artist}{s}",
                    "label_alt": "Now playing: {title:>8}{s}{artist}",
                    "separator": " ~ ",
                },
                steps=[_s(title="Song", artist="Artist"), _s(title="Song", artist=""), {"toggle": True}, _s()],
            ),
            Scenario(
                "playback_controls",
                steps=[
                    _s(playing=True),
                    _s(playing=False),
                    _s(playing=True, prev_enabled=False),
                    _s(playing=False, play_enabled=False, next_enabled=False),
                    {"session": None},
                    _s(playing=True),
                ],
            ),
            Scenario(
                "controls_right",
                {"controls_left": False},
                steps=[_s(), {"session": None}],
            ),
            Scenario(
                "controls_hidden",
                {"controls_hide": True},
                steps=[_s(), {"session": None}],
            ),
            Scenario(
                "controls_only",
                {"controls_only": True},
                steps=[_s(), _s(playing=False), {"session": None}],
            ),
            Scenario(
                "hide_empty_off",
                {"hide_empty": False},
                steps=[{"session": None}, _s(), {"session": None}],
            ),
            Scenario(
                "progress_bar",
                {"progress_bar": {"enabled": True, "alignment": "bottom"}},
                steps=[
                    _s(timeline_enabled=True, duration=200.0, position=50.0),
                    _s(timeline_enabled=True, duration=200.0, position=250.0),
                    _s(timeline_enabled=False, duration=200.0, position=50.0),
                    _s(timeline_enabled=True, duration=0.0, position=0.0),
                    _s(timeline_enabled=True, duration=700000.0, position=10.0),
                    _s(timeline_enabled=True, duration=3.0, position=1.0),
                ],
            ),
            Scenario(
                "progress_bar_disabled",
                {"progress_bar": {"enabled": False, "alignment": "top"}},
                steps=[_s(timeline_enabled=True, duration=200.0, position=100.0)],
            ),
            Scenario(
                "thumbnail",
                {"show_thumbnail": True},
                steps=[
                    _s(thumbnail=True),
                    _s(thumbnail=False),
                    _s(thumbnail=True, title=""),
                    {"session": None},
                ],
            ),
            Scenario(
                "thumbnail_off",
                {"show_thumbnail": False},
                steps=[_s(thumbnail=True)],
            ),
            Scenario(
                "menu_with_session",
                NO_BLUR,
                steps=[
                    _s(
                        title=LONG_TITLE * 4,
                        artist=LONG_ARTIST * 3,
                        timeline_enabled=True,
                        duration=3725.0,
                        position=61.0,
                        thumbnail=True,
                    ),
                    {"menu": True},
                    _s(title="Next", artist="", playing=False, prev_enabled=False, thumbnail=False),
                    {"session": None},
                    {"close_menu": True},
                ],
            ),
            Scenario(
                "menu_no_timeline",
                NO_BLUR,
                steps=[
                    _s(timeline_enabled=False, duration=0.0),
                    {"menu": True},
                    _s(app_id="Other.Player.App", title="Other", timeline_enabled=True, duration=90.0, position=5.0),
                    {"close_menu": True},
                    {"menu": True},
                ],
            ),
            Scenario(
                "menu_hide_source",
                {"media_menu": {"blur": False, "show_source": False, "max_title_size": 10, "max_artist_size": 8}},
                steps=[_s(title=LONG_TITLE, artist=LONG_ARTIST), {"menu": True}],
            ),
            Scenario(
                "menu_no_media",
                NO_BLUR,
                steps=[{"menu": True}],
            ),
        ],
    )
)
