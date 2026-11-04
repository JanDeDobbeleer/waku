# tiling: a built-in, opt-in tiler. Window events (WinEvent hooks) place a window that opens in the
# slot of its layout, per monitor and virtual desktop, with documented Win32 only. Inert while
# komorebi or GlazeWM runs. Depends on workspaces for the shown virtual desktop
# (virtual_desktops.h) and the komorebi process check (komorebi.h). dwmapi: the visible frame
# bounds and the cloak state; ole32: IVirtualDesktopManager.
waku_feature(
  NAME tiling
  DEPENDS workspaces
  SOURCES
    layouts.h
    layouts.cpp
    tiling_command.h
    tiling_command.cpp
    tiling_engine.h
    tiling_engine.cpp
    tiling_events.h
    tiling_model.h
    tiling_model.cpp
    tiling_rules.h
    tiling_rules.cpp
    window_placer.h
  WIN32_SOURCES
    win32/tiling_hooks_win.cpp
    win32/window_placer_win.cpp
  QML_SOURCES
    qml/tiling_service.h
    qml/tiling_service.cpp
  TESTS
    tests/test_tiling_command.cpp
    tests/test_tiling_engine.cpp
    tests/test_tiling_layouts.cpp
    tests/test_tiling_model.cpp
    tests/test_tiling_rules.cpp
    tests/test_tiling_service.cpp
  SYSTEM_LIBS
    advapi32
    dwmapi
    ole32
    user32
)
