"""Config vectors: normalised root/bars and per-widget outcomes, mirroring core.config and WidgetBuilder."""

import os
from importlib import import_module
from typing import Any

from vec_common import capture_logs

ENV_DEFINED = {"YASB_PARITY_VAR": "from-env"}
ENV_UNDEFINED = ("YASB_PARITY_UNDEFINED",)


def export_deprecations() -> dict[str, Any]:
    from core.validation import deprecation

    return {
        "deprecated_fields": deprecation.DEPRECATED_FIELDS,
        "renamed_fields": {key: list(value) for key, value in deprecation.RENAMED_FIELDS.items()},
        "scoped_deprecated_fields": deprecation.SCOPED_DEPRECATED_FIELDS,
        "scoped_renamed_fields": {
            model: {key: list(value) for key, value in fields.items()}
            for model, fields in deprecation.SCOPED_RENAMED_FIELDS.items()
        },
    }


def _error_details(exc: Any) -> list[dict[str, Any]]:
    """Location, message and type of each pydantic error; never the offending input."""
    return [{"loc": list(error["loc"]), "msg": error["msg"], "type": error["type"]} for error in exc.errors()]


def _widget_outcome(widget_configs: dict[str, Any], name: str) -> dict[str, Any]:
    from pydantic import ValidationError

    from core.utils.validation_errors import format_pydantic_errors_to_yaml

    widget_config = widget_configs.get(name, None)
    if not widget_config:
        return {"status": "undefined"}
    try:
        widget_type = widget_config["type"]
        module_name, class_name = widget_type.rsplit(".", 1)
        widget_cls = getattr(import_module(f"core.widgets.{module_name}"), class_name)
        schema = widget_cls.validation_schema
        options = widget_config.get("options", {})
        try:
            normalized = schema.model_validate(options).model_dump()
        except ValidationError as error:
            formatted = format_pydantic_errors_to_yaml(error)
            return {
                "status": "invalid_options",
                "type": widget_type,
                "errors": _error_details(error),
                "errors_yaml": formatted,
                "errors_indented": f"\n{formatted}".replace("\n", "\n      "),
            }
        return {"status": "ok", "type": widget_type, "normalized": normalized}
    except (AttributeError, ValueError, ModuleNotFoundError):
        return {"status": "unknown_type", "type": widget_config["type"]}
    except KeyError:
        return {"status": "missing_type"}
    except Exception as error:
        return {"status": "error", "exception": type(error).__name__}


def _placed_widgets(bars: dict[str, Any]) -> dict[str, list[str]]:
    placed: dict[str, list[str]] = {}
    for bar_name, bar in bars.items():
        if not bar["enabled"]:
            continue
        widgets = bar["widgets"]
        for widget_name in (*widgets["left"], *widgets["center"], *widgets["right"]):
            if bar_name not in placed.setdefault(widget_name, []):
                placed[widget_name].append(bar_name)
    return placed


def run_case(text: str) -> dict[str, Any]:
    import yaml
    from pydantic import ValidationError

    from core.config import parse_env
    from core.utils.validation_errors import format_pydantic_errors_to_yaml
    from core.validation.config import YasbConfig

    result: dict[str, Any] = {}
    with capture_logs() as logs:
        try:
            raw = yaml.safe_load(text)
        except yaml.YAMLError as error:
            return {"status": "parse_error", "exception": type(error).__name__}
        raw = parse_env({} if raw is None else raw)
        result["root_is_mapping"] = isinstance(raw, dict)
        try:
            config = YasbConfig(**(raw if isinstance(raw, dict) else {}))
        except ValidationError as error:
            result.update(
                status="validation_error",
                errors=_error_details(error),
                errors_yaml=format_pydantic_errors_to_yaml(error),
                warnings=logs.records,
            )
            return result

        normalized = config.model_dump()
        placed = _placed_widgets(normalized["bars"])
        result.update(
            status="ok",
            normalized=normalized,
            placed_widgets=sorted(placed),
            widget_bars=placed,
            widgets={name: _widget_outcome(config.widgets, name) for name in placed},
        )
        result["warnings"] = logs.records
    return result


def export_config(cases: dict[str, str]) -> dict[str, Any]:
    saved = {name: os.environ.get(name) for name in (*ENV_DEFINED, *ENV_UNDEFINED)}
    os.environ.update(ENV_DEFINED)
    for name in ENV_UNDEFINED:
        os.environ.pop(name, None)
    try:
        return {name: {"input": f"inputs/config/{name}.yaml", **run_case(text)} for name, text in sorted(cases.items())}
    finally:
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
