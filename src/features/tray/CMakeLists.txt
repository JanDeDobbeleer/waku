# tray: the notification area. The window host (a Shell_TrayWnd window of waku's own), the
# message parser, icon state, pins, and the Tray widget. No dependencies on other features.
waku_feature(
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
    win32/tray_win.h
    win32/tray_win.cpp
    win32/tray_window_host.h
    win32/tray_window_host.cpp
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
    tests/test_systray_state.cpp
    tests/test_tray_icon_bits.cpp
    tests/test_tray_icon_registry.cpp
    tests/test_tray_model.cpp
    tests/test_tray_pins.cpp
    tests/test_tray_pipe.cpp
    tests/test_tray_widget.cpp
    tests/test_tray_window_host.cpp
  SYSTEM_LIBS
    user32
)

# tray_state.cpp reads and writes the per-screen JSON state.
target_link_libraries(waku_tray PRIVATE nlohmann_json::nlohmann_json)
