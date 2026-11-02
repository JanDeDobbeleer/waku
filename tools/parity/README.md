# Parity oracle (`tools/parity`)

Exports JSON vectors from the Python YASB build. The C++ unit tests replay them, so the Python app stays the oracle for config validation, CSS processing, adaptive-bar geometry and widget labels.

## Run

Use the YASB virtualenv (it has PyQt6, pydantic and the pinned `qt-css-engine`, commit `fadd1a992`):

```powershell
D:\yasb\.venv\Scripts\python.exe tools\parity\export_vectors.py `
    --yasb D:\yasb `
    --config tools\parity\fixtures\config\user.yaml `
    --styles tools\parity\fixtures\css\user\styles.css `
    --out tests\vectors `
    --schema-out schema\schema.json
```

`--config` and `--styles` default to the committed copies of the user's files. To refresh them from the live files, pass `C:\Users\<you>\.config\yasb\config.yaml` and `styles.css`; they are read, never written.

The exporter mirrors `tests/conftest.py`: it runs in a scratch folder under `tools/parity/.sandbox` with `YASB_CONFIG_HOME` and `LOCALAPPDATA` redirected and `QT_QPA_PLATFORM=offscreen`, so no real YASB state is touched. Output is sorted-key UTF-8 JSON with LF endings and is byte-identical between runs.

Tests: `D:\yasb\.venv\Scripts\python.exe tools\parity\tests\test_export.py` (also collected by pytest).

## Output (`tests/vectors`)

| File | Content |
| --- | --- |
| `manifest.json` | YASB commit and version, qt-css-engine commit, vector counts |
| `config.json` | per fixture: `YasbConfig.model_dump()` as `normalized`, deprecation warnings, and per placed widget either `ok` (normalized options), `invalid_options` (errors, YAML text), `unknown_type`, `missing_type`, `undefined` or `error` |
| `deprecations.json` | `DEPRECATED_FIELDS`, `RENAMED_FIELDS` and the scoped tables |
| `css.json` | per stylesheet fixture: `CSSProcessor` output (`processed`), the cleaned `qss`, `extract_rules` `rules`, and log records |
| `islands.json` | adaptive island geometry per case of `tests/bar/test_adaptive_bar.py` (islands, overhang, rects, shape paths) |
| `labels/<widget>.json` | per scenario: options, fake service data per step, and the rendered label text, CSS class and visibility |
| `inputs/` | the exact config and CSS inputs the vectors were computed from |

In `css.json`, `${DIR}` is the entry stylesheet's folder and `${CONFIG_HOME}` the sandbox config folder. Tests substitute their own paths.

## Fixtures

`fixtures/config/*.yaml` and `fixtures/css/<case>/styles.css`. Adding a file adds a vector on the next run. `user.yaml` and `css/user` are the user's real files.

## Adding a widget's label vectors

Label exporters register in `vec_labels.py` through `label_registry.register(LabelExporter(...))`: a `build` that makes the real widget with fake service data, an `apply_step` that feeds new data, and a list of `Scenario`s. cpu, memory and battery live in `vec_labels.py`; github (`vec_github.py`) and the two komorebi widgets (`vec_komorebi.py`, which also supplies its own snapshot, normalized options and recorded `komorebic` commands) are their own modules, imported at the end of `vec_labels.py`. Options come from the user's config for the widget type, with per-scenario patches. wifi and bluetooth live in `vec_labels_network.py` (fake worker, menu, manager and host-name lookup).

## Secrets

Inputs pass through `scrub_secrets` before use: values under token/key/secret-like keys and token-shaped strings become `<REDACTED>`. The last run redacted nothing (the user's github widget has no token). The user's wallpaper folder path (`C:/Users/jande/.config/wallpapers`) is kept as an ordinary option.

## Schema

`schema/schema.json` is regenerated from the pydantic models with `core.validation.export_schema` (`--schema-out`), not copied: `D:\yasb\schema.json` at the pinned commit lacks the VPN-aware wifi fields (`vpn_icon`) that the local models have.
