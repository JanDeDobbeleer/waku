"""Label vectors for the github widget.

GitHubDataManager is stubbed (no network, no timer) and the token lookup returns a fixed
placeholder, so the real GithubWidget is built offscreen against fake notification lists. A step
is {"notifications": [{"unread": bool}, ...]} (or {"unread": n, "read": m}) or {"toggle": true}.
"""

from typing import Any

from label_registry import LabelExporter, Scenario, register


class _Fake:
    data: list[dict[str, Any]] = []


def _setup() -> Any:
    from core.widgets.yasb import github as module

    manager = module.GitHubDataManager
    saved = {name: manager.__dict__[name] for name in ("initialize", "get_data", "register_callback")}
    saved_token = module.get_saved_token
    _Fake.data = []
    manager.initialize = classmethod(lambda cls, *args, **kwargs: None)
    manager.register_callback = classmethod(lambda cls, callback: None)
    manager.get_data = classmethod(lambda cls: list(_Fake.data))
    module.get_saved_token = lambda: "vector-placeholder"

    def restore() -> None:
        for name, value in saved.items():
            setattr(manager, name, value)
        module.get_saved_token = saved_token
        _Fake.data = []

    return restore


def _build(config: Any, scenario: Scenario) -> Any:
    from core.widgets.yasb.github import GithubWidget

    _Fake.data = []
    return GithubWidget(config)


def _notifications(step: dict[str, Any]) -> list[dict[str, Any]]:
    if "notifications" in step:
        return [dict(item) for item in step["notifications"]]
    items = [{"unread": True} for _ in range(step.get("unread", 0))]
    items += [{"unread": False} for _ in range(step.get("read", 0))]
    return items


def _step(widget: Any, step: dict[str, Any]) -> None:
    if step.get("toggle"):
        widget._toggle_label()
        return
    _Fake.data = _notifications(step)
    widget._update_label()


register(
    LabelExporter(
        widget="github",
        widget_type="yasb.github.GithubWidget",
        build=_build,
        apply_step=_step,
        setup=_setup,
        scenarios=[
            Scenario("placeholder_only"),
            Scenario(
                "user_options_counts",
                steps=[{"unread": 0}, {"unread": 1}, {"unread": 5, "read": 3}, {"unread": 0, "read": 2}],
            ),
            Scenario(
                "alt_label_toggle",
                steps=[{"unread": 3}, {"toggle": True}, {"unread": 0}, {"toggle": True}, {"unread": 2}],
            ),
            Scenario(
                "hide_empty",
                {"hide_empty": True},
                steps=[{"unread": 0}, {"unread": 2}, {"unread": 0, "read": 4}],
            ),
            Scenario(
                "custom_labels_and_class",
                {
                    "label": "<span class='gh extra'></span> {data}",
                    "label_alt": "{data} unread <span></span>",
                },
                steps=[{"unread": 4}, {"toggle": True}, {"unread": 0}, {"unread": 9}],
            ),
            Scenario(
                "dot_disabled",
                {"notification_dot": {"enabled": False}},
                steps=[{"unread": 1}, {"toggle": True}, {"unread": 0}],
            ),
            Scenario(
                "whitespace_part_index_shift",
                {"label": " <span></span> {data}", "label_alt": "{data}"},
                steps=[{"unread": 2}, {"unread": 0}],
            ),
        ],
    )
)
