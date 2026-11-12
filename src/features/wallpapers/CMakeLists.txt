# wallpapers: the wallpaper folder, set and cycle wallpapers, the picker gallery and its hotkey, and
# the animated change (a Qt Quick overlay on WorkerW) and the lock screen picture that follows a
# change. workspaces applies a wallpaper across virtual desktops; the hotkey is in kernel::Hotkeys.
# wallpaper_command.h is the `wallpaper` verb of the control pipe and the WallpaperControl
# interface the service implements; waku.exe adds the verb to the bar's control handler.
waku_feature(
  NAME wallpapers
  DEPENDS workspaces
  SOURCES
    desktop_wallpaper.h
    lock_screen.h
    lock_screen.cpp
    wallpaper_files.h
    wallpaper_files.cpp
    wallpaper_command.h
    wallpaper_command.cpp
    wallpaper_manager.h
    wallpaper_manager.cpp
  WIN32_SOURCES
    win32/desktop_wallpaper_win.cpp
    win32/lock_screen_win.cpp
    win32/window_focus.h
    win32/window_focus.cpp
  QML_SOURCES
    qml/wallpaper_settings.h
    qml/wallpaper_settings.cpp
    qml/wallpapers_service.h
    qml/wallpapers_service.cpp
  QML_FILES
    qml/WallpaperGallery.qml
    qml/Wallpapers.qml
  TESTS
    tests/test_wallpaper_command.cpp
    tests/test_wallpaper_lock_screen.cpp
    tests/test_wallpaper_manager.cpp
    tests/test_wallpaper_service.cpp
    tests/test_wallpapers.cpp
  SYSTEM_LIBS
    advapi32
    ole32
    shell32
    shlwapi
    user32
    uuid
    windowsapp
)

# wallpaper_files.h names nlohmann::json in its API.
target_link_libraries(waku_wallpapers PUBLIC nlohmann_json::nlohmann_json)
