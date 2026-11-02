# apps: the app catalogue for a launcher. Start Menu shortcuts (IShellLink) and the Store apps of
# shell:AppsFolder as Quickshell-shaped DesktopEntry objects, ranked search with frecency, launch
# (also elevated), and the asynchronous image://appicon provider. No dependencies on other
# features. `import Quickshell` reaches it as DesktopEntries (src/quickshell/core/qml). See
# design.md.
waku_feature(
  NAME apps
  SOURCES
    apps_platform.h
    catalog.h
    catalog.cpp
    frecency.h
    frecency.cpp
    rank.h
    rank.cpp
  WIN32_SOURCES
    win32/apps_folder_win.cpp
    win32/launch_win.cpp
    win32/start_menu_win.cpp
  QML_SOURCES
    qml/app_icon_provider.h
    qml/app_icon_provider.cpp
    qml/apps_model.h
    qml/apps_model.cpp
    qml/apps_service.h
    qml/apps_service.cpp
    qml/desktop_entry.h
    qml/desktop_entry.cpp
  TESTS
    tests/test_apps_catalog.cpp
    tests/test_apps_rank.cpp
    tests/test_apps_service.cpp
  SYSTEM_LIBS
    ole32
    shell32
    user32
    uuid
)
