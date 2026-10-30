# The QML module `Quickshell` (and `Quickshell.Wayland` in wayland/): wabar's shell types under the
# names and import of Quickshell v0.3, so `import Quickshell` in a ported shell resolves. The C++
# types live in wabar_host and are registered here a second time with QML_FOREIGN
# (qml/quickshell_types.h); the QML types are thin wrappers (qml/*.qml). A shell may import both
# `wabar` and `Quickshell`: the names resolve to the same types.
#
# The `Quickshell` singleton is qml/Quickshell.qml: `screens` from the host's ScreenList, the
# rest forwarded to wabar.services.QuickshellSingleton (a QML file because the host unit cannot
# include a feature's headers). `DesktopEntries` (qml/DesktopEntries.qml) forwards to
# wabar.services.AppsService the same way; its DesktopEntry and DesktopAction types are reached
# through the qmldir `import wabar.services` line (IMPORTS below), as Quickshell.Io does.
# `Quickshell.Io` is assembled in src/modules/qml_modules.cmake.
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
  DEPENDENCIES wabar
  IMPORTS wabar.services
  QML_FILES ${quickshell_qml_files}
  SOURCES qml/quickshell_types.h
)
target_include_directories(wabar_quickshell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qml)
target_link_libraries(wabar_quickshell
  PUBLIC Qt6::Quick Qt6::Qml wabar_host
  PRIVATE wabar_options)
wabar_qt_generated_code(wabar_quickshell)

add_subdirectory(wayland)
