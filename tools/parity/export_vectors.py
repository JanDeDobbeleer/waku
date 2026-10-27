"""Export oracle vectors from the Python YASB checkout for the C++/Qt port.

Run with the YASB virtualenv interpreter:

    D:\\yasb\\.venv\\Scripts\\python.exe tools\\parity\\export_vectors.py --yasb D:\\yasb --out tests\\vectors
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from sandbox import yasb_sandbox  # noqa: E402
from vec_common import list_fixture_files, read_text, scrub_secrets, write_json, write_text  # noqa: E402

DEFAULT_CONFIG = HERE / "fixtures" / "config" / "user.yaml"
DEFAULT_STYLES = HERE / "fixtures" / "css" / "user" / "styles.css"
SCRATCH = HERE / ".sandbox"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--yasb", type=Path, required=True, help="Python YASB checkout")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG, help="config.yaml for the 'user' case")
    parser.add_argument("--styles", type=Path, default=DEFAULT_STYLES, help="styles.css for the 'user' case")
    parser.add_argument("--out", type=Path, required=True, help="vector output directory")
    parser.add_argument("--schema-out", type=Path, help="also regenerate schema.json from the pydantic models")
    return parser.parse_args(argv)


def git_commit(path: Path) -> str:
    try:
        result = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True, text=True)
        return result.stdout.strip() or "unknown"
    except OSError:
        return "unknown"


def qt_css_engine_info() -> dict[str, str]:
    from importlib import metadata

    dist = metadata.distribution("qt-css-engine")
    info = {"version": dist.version, "commit": "unknown"}
    direct_url = dist.read_text("direct_url.json")
    if direct_url:
        info["commit"] = json.loads(direct_url).get("vcs_info", {}).get("commit_id", "unknown")
    return info


def load_inputs(args: argparse.Namespace) -> tuple[dict[str, str], dict[str, Path], dict[str, int]]:
    """Config texts by case and CSS fixture folders by case, secrets scrubbed; plus scrub hit counts."""
    hits: dict[str, int] = {}
    configs: dict[str, str] = {}
    for path in list_fixture_files(HERE / "fixtures" / "config", ".yaml"):
        source = args.config if path.stem == "user" else path
        configs[path.stem], hits[f"config/{path.stem}"] = scrub_secrets(read_text(source))

    staging = SCRATCH / "css-inputs"
    css_dirs: dict[str, Path] = {}
    for folder in sorted(p for p in (HERE / "fixtures" / "css").iterdir() if p.is_dir()):
        target = staging / folder.name
        for source in sorted(folder.rglob("*")):
            if not source.is_file():
                continue
            destination = target / source.relative_to(folder)
            destination.parent.mkdir(parents=True, exist_ok=True)
            entry = args.styles if folder.name == "user" and source.name == "styles.css" else source
            text, count = scrub_secrets(read_text(entry), scan_keys=False)
            hits[f"css/{folder.name}/{source.relative_to(folder).as_posix()}"] = count
            write_text(destination, text)
        css_dirs[folder.name] = target
    return configs, css_dirs, hits


def user_widget_options(config_text: str) -> dict[str, dict]:
    """Raw options of the user's widgets, keyed by widget type (first definition wins)."""
    import yaml

    from core.config import parse_env

    raw = parse_env(yaml.safe_load(config_text) or {})
    options: dict[str, dict] = {}
    for entry in (raw.get("widgets") or {}).values():
        if isinstance(entry, dict) and isinstance(entry.get("type"), str):
            options.setdefault(entry["type"], entry.get("options") or {})
    return options


def regenerate_schema(destination: Path, workdir: Path) -> None:
    import os

    from core.validation.export_schema import export_schema

    previous = Path.cwd()
    os.chdir(workdir)
    try:
        export_schema()
    finally:
        os.chdir(previous)
    schema = json.loads((workdir / "schema.json").read_text(encoding="utf-8"))
    write_text(destination, json.dumps(schema, indent=2) + "\n")


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    out = args.out.resolve()
    yasb_root = args.yasb.resolve()

    with yasb_sandbox(yasb_root, SCRATCH) as sandbox_config:
        from PyQt6.QtWidgets import QApplication

        app = QApplication.instance() or QApplication([])
        configs, css_dirs, scrub_hits = load_inputs(args)

        import vec_config
        import vec_css
        import vec_islands
        import vec_labels

        write_json(out / "deprecations.json", vec_config.export_deprecations())
        config_vectors = vec_config.export_config(configs)
        write_json(out / "config.json", config_vectors)
        write_json(out / "css.json", vec_css.export_css(css_dirs, sandbox_config))
        islands = vec_islands.export_islands()
        write_json(out / "islands.json", islands)

        label_vectors = vec_labels.export_label_vectors(user_widget_options(configs["user"]))
        for widget, vectors in label_vectors.items():
            write_json(out / "labels" / f"{widget}.json", vectors)

        for name, text in configs.items():
            write_text(out / "inputs" / "config" / f"{name}.yaml", text)
        for name, folder in css_dirs.items():
            for source in sorted(folder.rglob("*")):
                if source.is_file():
                    destination = out / "inputs" / "css" / name / source.relative_to(folder)
                    write_text(destination, read_text(source))

        from settings import BUILD_VERSION

        write_json(
            out / "manifest.json",
            {
                "yasb_commit": git_commit(yasb_root),
                "yasb_version": BUILD_VERSION,
                "qt_css_engine": qt_css_engine_info(),
                "counts": {
                    "config_cases": len(config_vectors),
                    "css_cases": len(css_dirs),
                    "island_cases": len(islands["cases"]),
                    "label_scenarios": {name: len(items) for name, items in sorted(label_vectors.items())},
                },
                "secrets_redacted": sum(scrub_hits.values()),
            },
        )

        if args.schema_out:
            regenerate_schema(args.schema_out.resolve(), Path(sandbox_config))
        app.processEvents()

    for name, count in sorted(scrub_hits.items()):
        if count:
            print(f"scrubbed {count} secret-like value(s) in {name}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
