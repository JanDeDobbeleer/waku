# Copilot instructions

`AGENTS.md` at the repository root is the source of truth (build and test commands, rules, style,
error handling, live-desktop rules, commit conventions). Definition of done: `tools\check.ps1`
exits 0. The structure in short:

- wabar is a C++20/Qt 6.10 Qt Quick bar for Windows, configured in QML; C++ namespaces are `wabar::<unit>`.
- `src/kernel`: shared helpers every unit may use. `src/control`: control pipe and `wabarc` protocol.
- `src/host`: bar windows, app bar, theme, QML module `wabar`. `src/ui`: shared QML controls.
- `src/features/<f>`: one folder per capability (audio, clock, tray ...), declared with `wabar_feature()`.
- `src/app`: the `wabar` and `wabarc` executables. `src/modules`: assembles `wabar.widgets` and `wabar.services`.
- `tests/`: the one test binary (`wabar_tests`) and integration tests; unit tests sit in each unit.
- Dependencies point one way: `app` -> `features` -> (`host`, `control`, `ui`) -> `kernel`; `tools\check.ps1` enforces it.
- Hook DLL `WabarTrayHook[_arm64].dll` is injected into Explorer; tests never inject it.
- Commits are conventional: `type(scope): subject`, scope = a unit or feature name.
