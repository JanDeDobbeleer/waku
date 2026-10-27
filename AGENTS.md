# Agent guide (yasb-native)

Read this before changing anything. It applies to every agent session (GitHub Copilot CLI) and to
humans. Background: `docs/architecture.md`, `docs/building.md`, `SECURITY.md`.

yasb-native is a C++20/Qt 6.10 port of YASB. The Python build (`D:\yasb`, branch `local/build`) is
the behavioural reference: when in doubt, match what Python YASB does.

## Build and test

Run every step in one `cmd.exe` process after the MSVC environment script; environment changes do
not survive between separate `cmd /c` calls. `vcvars*.bat` live in
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build`. They point
`VCPKG_ROOT` at the bundled vcpkg, so set it to `C:\vcpkg` afterwards.

```powershell
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build'

# ARM64 debug: configure, build, test (the default preset)
cmd /c "call `"$vc\vcvarsarm64.bat`" && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-debug && cmake --build --preset arm64-debug && ctest --test-dir build\arm64-debug --output-on-failure --timeout 120"

# ARM64 release
cmd /c "call `"$vc\vcvarsarm64.bat`" && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-release && cmake --build --preset arm64-release && ctest --test-dir build\arm64-release --output-on-failure --timeout 120"

# x64 release, cross-built from the ARM64 host (tests run under emulation)
cmd /c "call `"$vc\vcvarsarm64_amd64.bat`" && set VCPKG_ROOT=C:\vcpkg&& cmake --preset x64-release && cmake --build --preset x64-release && ctest --test-dir build\x64-release --output-on-failure --timeout 120"
```

`ctest` needs `QT_QPA_PLATFORM=offscreen`; the CMake test properties set it, so `ctest --test-dir`
works without the test preset. Presets live in `CMakePresets.json`; `compile_commands.json` is
written to `build\<preset>`.

Parity vectors (needs the YASB virtualenv): see `tools/parity/README.md`.

## Definition of done

`tools\check.ps1` exits 0. It runs the clang-format check, the generated-file drift check, the
`arm64-debug` build and `ctest`. `tools\check.ps1 -Fast` runs only the first two.

Git hooks in `tools/hooks` run the fast checks on staged files and validate the commit message.
Enable them once per clone:

```powershell
git config core.hooksPath tools/hooks
```

## Layering

Dependencies point one way only. Do not add an include that goes against the arrow, even though
the single `yasb_core` CMake target does not stop you yet (plan unit R7 splits it).

```
app  ->  widgets  ->  core/{widgets,bar,util} (ui)  ->  core/{css,config}  ->  platform
```

| Layer | Folders | May include |
|---|---|---|
| app | `app/` | everything below |
| widgets | `widgets/` | ui, css, config, platform |
| ui | `core/bar/`, `core/widgets/`, `core/util/` | css, config, Qt |
| css / config | `core/css/`, `core/config/` | Qt, nlohmann, yaml-cpp; not each other |
| platform | `platform/` | Win32, Qt Core only |

Core must never include `widgets/` (today `core/widgets/CMakeLists.txt` compiles the stat widgets
into the core target; that is a known violation, not a pattern to copy). Details:
`docs/architecture.md`.

## Style

- `.clang-format` (Google based, 2-space indent, 100 columns) is the formatter; run
  `clang-format -i` on every `.cpp`/`.h` you touch. clang-format lives on `PATH` or in
  `C:\Program Files\LLVM\bin`.
- Members use a trailing underscore: `name_`. Do not add new `m_name` members.
- Includes are rooted at the repo: `#include "core/widgets/base_widget.h"`, never `"base_widget.h"`.
- Qt string literals: `QStringLiteral("...")`. Do not introduce `u"..."_s` or `QLatin1String` in new
  code except where an API needs a Latin-1 view.
- `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `UNICODE` come from the `yasb_options` target; do not redefine
  them locally.
- `.editorconfig` defines whitespace and line endings: CRLF for sources, docs and generated option
  headers, LF for the parity vectors and fixtures.

## Error handling

Exceptions never cross a Qt boundary: slots, event handlers, `paintEvent`, timers and any callback
Qt invokes. Every entry point from Qt into our code either uses non-throwing APIs (`json::value`,
`find`, `std::from_chars`, `std::optional` returns) or wraps its body in `try`/`catch` and logs.
At API boundaries return values or `std::optional`; exceptions thrown and caught inside one module
are fine. Never use the `const` `nlohmann::json::operator[]` on a key that may be missing. See
`docs/architecture.md`.

## Generated files: never hand-edit

| Files | Source of truth | Regenerate with |
|---|---|---|
| `core/config/options/*.h` | `schema/schema.json` | `python core\config\options\generate_options.py` |
| `tests/vectors/**`, `schema/schema.json` | Python YASB + `tools/parity/fixtures` | `tools\parity\export_vectors.py` (see `tools/parity/README.md`) |

The parity vectors are the oracle. If a C++ test disagrees with a vector, fix the C++, not the
vector. Vectors and fixtures are LF and byte-exact; never let an editor re-save them.

## Live desktop rules

- Tests and tooling always pass `--config-home <dir>` to `yasb-native.exe`. Never run it against the
  user's real config (`%USERPROFILE%\.config\yasb`) and never write to that folder.
- `--register-app-bar` reserves screen work area. Only `tools/ab` passes it.
- Prefer `--smoke`, `--version` and `--screenshot <png>` with `QT_QPA_PLATFORM=offscreen` for checks.

## Python parity of startup

Port every Python startup side effect: environment variables, Qt attributes, application flags.
Example: Python YASB sets `QT_QPA_PLATFORM=windows:fontengine=gdi` in `src/env.py`
(`set_font_engine`); missing it made Qt use DirectWrite and map every installed font (2,105 handles,
206 MiB working set). When you port a startup path, read the Python entry points (`src/main.py`,
`src/env.py`, `src/settings.py`) for side effects, not just the feature code.

## Undocumented Windows APIs

Every call to an undocumented API (`GetProcAddress` on an unexported/undocumented symbol, a
hand-laid struct, a numeric constant missing from the SDK) must be added to the allowlist in
`docs/architecture.md` with the Windows build it was verified on and its fallback, in the same
commit. It must fail soft (fall back or skip) when the symbol is missing or the call fails.

## New widget checklist

A widget is not done until it ships all of:

1. The typed options struct from `core/config/options/<widget>.h`, wired through `WidgetContext`
   (no restated defaults in C++).
2. A label-vector exporter in `tools/parity/vec_labels.py` and its regenerated vectors.
3. A C++ parity test that replays those vectors under `tests/widgets/`.
4. An updated row in `docs/parity-checklist.md`.
5. For every new thread or `QThread`: a comment next to the class saying which thread owns what and
   how it is stopped.
6. No new singleton without a row (and a reason) in the singleton table in `docs/architecture.md`.

## Commits

Conventional commits: `type(scope): subject`, imperative, lower case, no trailing period.

- Types: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`, `style`, `revert`.
- Scopes: `config`, `css`, `bar`, `widgets`, `app`, `platform`, `parity`, `ab`, `build`, `docs`,
  `test`, `style`, `chore`. The scope is optional.
- One logical change per commit; `tools\check.ps1` green before committing.
- Agent-authored commits end with the trailer
  `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>` as the last line.
