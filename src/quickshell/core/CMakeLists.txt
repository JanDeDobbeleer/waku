# The QML module `Quickshell`: wabar's shell types under the names and import of Quickshell v0.3,
# so `import Quickshell` in a ported shell resolves. The C++ types live in wabar_host (host/qml/)
# and in two features; qml/quickshell_types.h and qml/quickshell_types_core.h register them here a
# second time with QML_FOREIGN, so a shell may import both `wabar` and `Quickshell` and gets the
# same types. Two names are the exception: PopupWindow and Variants are QML wrappers here
# (qml/PopupWindow.qml, qml/Variants.qml), so `Quickshell.PopupWindow` and `wabar.PopupWindow` are
# two distinct QML types with the same behaviour.
#
# The `Quickshell` singleton is qml/Quickshell.qml: `screens` from the host's ScreenList, the rest
# forwarded to wabar.services.QuickshellSingleton. `DesktopEntries` (qml/DesktopEntries.qml)
# forwards to wabar.services.AppsService the same way. DesktopEntry, DesktopAction (apps) and
# ElapsedTimer (quickshell_io) are registered under their Quickshell names by
# qml/quickshell_types_core.h. The module depends on wabar.services but does not re-export it:
# `import Quickshell` shows Quickshell's type names and no others, so a shell's own Wallpapers.qml
# is never shadowed.
set(quickshell_qml_files qml/DesktopEntries.qml qml/PopupWindow.qml qml/Variants.qml qml/Quickshell.qml)
set_source_files_properties(qml/Quickshell.qml qml/DesktopEntries.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE)
foreach(file IN LISTS quickshell_qml_files)
  cmake_path(GET file FILENAME name)
  set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_library(wabar_quickshell STATIC)
qt_add_qml_module(wabar_quickshell
  URI Quickshell
  VERSION 0.1
  NO_GENERATE_EXTRA_QMLDIRS
  DEPENDENCIES wabar wabar.services
  QML_FILES ${quickshell_qml_files}
  SOURCES qml/quickshell_types.h
          qml/quickshell_types_core.h
)
target_include_directories(wabar_quickshell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qml)
# wabar_services: the staticMetaObject of DesktopEntry, DesktopAction and ElapsedTimer.
target_link_libraries(wabar_quickshell
  PUBLIC Qt6::Quick Qt6::Qml wabar_host
  PRIVATE wabar_options wabar_services)
wabar_qt_generated_code(wabar_quickshell)

