# wallpapers: the wallpaper folder, set and cycle wallpapers, the picker gallery and its hotkey, and
# the animated change (a Qt Quick overlay on WorkerW). No dependencies on other features.
# The service implements wabar::WallpaperControl (control/wallpaper_control.h), the interface the
# bar's control handler calls.
wabar_feature(
  NAME wallpapers
  SOURCES
    desktop_wallpaper.h
    thumbnail_cache.h
    thumbnail_cache.cpp
    wallpaper_files.h
    wallpaper_files.cpp
    wallpaper_hotkey.h
    wallpaper_hotkey.cpp
    wallpaper_layout.h
    wallpaper_layout.cpp
    wallpaper_manager.h
    wallpaper_manager.cpp
  WIN32_SOURCES
    win32/desktop_wallpaper_win.cpp
    win32/wallpaper_hotkey_win.h
    win32/wallpaper_hotkey_win.cpp
    win32/window_focus.h
    win32/window_focus.cpp
    win32/workerw.h
    win32/workerw.cpp
  QML_SOURCES
    qml/wallpaper_settings.h
    qml/wallpaper_settings.cpp
    qml/wallpaper_transition.h
    qml/wallpaper_transition.cpp
    qml/wallpapers_service.h
    qml/wallpapers_service.cpp
  QML_FILES
    qml/WallpaperGallery.qml
    qml/Wallpapers.qml
  TESTS
    tests/test_wallpaper_manager.cpp
    tests/test_wallpaper_transition.cpp
  SYSTEM_LIBS
    advapi32
    ole32
    shell32
    shlwapi
    user32
    uuid
)

# wallpaper_files.h names nlohmann::json in its API.
target_link_libraries(wabar_wallpapers PUBLIC nlohmann_json::nlohmann_json)

# qml/wallpapers_service.h includes control/wallpaper_control.h.
target_link_libraries(wabar_wallpapers PUBLIC wabar_control)
