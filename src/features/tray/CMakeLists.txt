# tray: the notification area. The Explorer hook (hook/, a DLL injected into Explorer), the pipe
# parser, icon state, pins, and the Tray widget. No dependencies on other features.
wabar_feature(
  NAME tray
  SOURCES
    icon_bits.h
    icon_bits.cpp
    icon_scale.h
    icon_scale.cpp
    tray_click.h
    tray_click.cpp
    tray_host.h
    tray_icon_registry.h
    tray_icon_registry.cpp
    tray_pipe.h
    tray_pipe.cpp
    tray_service.h
    tray_service.cpp
    tray_state.h
    tray_state.cpp
    tray_types.h
  WIN32_SOURCES
    win32/tray_hook.h
    win32/tray_hook.cpp
    win32/tray_win.h
    win32/tray_win.cpp
  QML_SOURCES
    qml/systray.h
    qml/systray.cpp
    qml/tray_icon_model.h
    qml/tray_icon_model.cpp
    qml/tray_image_provider.h
    qml/tray_image_provider.cpp
    qml/tray_pins.h
    qml/tray_pins.cpp
  QML_FILES
    qml/Tray.qml
    qml/TrayIconButton.qml
  TESTS
    tests/test_systray.cpp
    tests/test_systray_click.cpp
    tests/test_systray_icon_scale.cpp
    tests/test_systray_pipe.cpp
    tests/test_systray_state.cpp
    tests/test_tray_icon_bits.cpp
    tests/test_tray_icon_registry.cpp
    tests/test_tray_model.cpp
    tests/test_tray_pins.cpp
    tests/test_tray_widget.cpp
  SYSTEM_LIBS
    psapi
    user32
  SUBTARGET_SOURCES
    hook/trayhook.cpp
)

# Pipe and watchdog mutex names shared with the DLL (hook/), set once in the root CMakeLists.txt.
target_compile_definitions(wabar_tray PUBLIC
  WABAR_TRAY_PIPE_NAME="${WABAR_TRAY_PIPE_NAME}" WABAR_TRAY_MUTEX_NAME="${WABAR_TRAY_MUTEX_NAME}")
# tray_state.cpp reads and writes the per-screen JSON state.
target_link_libraries(wabar_tray PRIVATE nlohmann_json::nlohmann_json)

# The hook DLL builds next to wabar.exe.
add_subdirectory(hook)
