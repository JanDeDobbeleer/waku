"""CSS vectors: CSSProcessor output and qt-css-engine rule extraction."""

import shutil
from pathlib import Path
from typing import Any

from vec_common import capture_logs, jsonable

DIR_PLACEHOLDER = "${DIR}"


def _anonymise(text: str, directory: Path) -> str:
    """Scratch paths become placeholders: ${DIR} is the entry stylesheet's folder, ${CONFIG_HOME} the sandbox root."""
    for folder, placeholder in ((directory, DIR_PLACEHOLDER), (directory.parent.parent, "${CONFIG_HOME}")):
        for spelling in (str(folder).replace("\\", "\\\\"), str(folder), folder.as_posix()):
            text = text.replace(spelling, placeholder)
    return text


def _anonymise_value(value: Any, directory: Path) -> Any:
    if isinstance(value, str):
        return _anonymise(value, directory)
    if isinstance(value, dict):
        return {key: _anonymise_value(item, directory) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_anonymise_value(item, directory) for item in value]
    return value

def run_case(directory: Path) -> dict[str, Any]:
    from core.utils.css_processor import CSSProcessor
    from qt_css_engine import extract_rules

    entry = directory / "styles.css"
    result: dict[str, Any] = {}
    with capture_logs() as logs:
        processed = CSSProcessor(str(entry)).process()
        result["processed"] = _anonymise(processed, directory)
        try:
            qss, rules = extract_rules(processed)
            result["qss"] = _anonymise(qss, directory)
            result["rules"] = _anonymise_value(jsonable(list(rules)), directory)
        except Exception as error:
            result["extract_error"] = type(error).__name__
    result["log"] = [
        {**record, "message": _anonymise(record["message"], directory)} for record in logs.records
    ]
    return result


def export_css(fixtures: dict[str, Path], sandbox_config: Path) -> dict[str, Any]:
    """`fixtures` maps case name to a folder holding styles.css and any files it imports."""
    cases: dict[str, Any] = {}
    for name, source in sorted(fixtures.items()):
        target = sandbox_config / "css" / name
        shutil.copytree(source, target)
        cases[name] = {"input": f"inputs/css/{name}/styles.css", **run_case(target)}
    return cases
