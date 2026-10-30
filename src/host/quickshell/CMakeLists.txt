# The QML module `Quickshell` (and `Quickshell.Wayland` in wayland/): wabar's shell types under the
# names and import of Quickshell v0.3, so `import Quickshell` in a ported shell resolves. The C++
# types live in wabar_host and are registered here a second time with QML_FOREIGN
# (qml/quickshell_types.h); the QML types are thin wrappers (qml/*.qml). A shell may import both
# `wabar` and `Quickshell`: the names resolve to the same types.
#
# Not here yet: the `Quickshell` singleton (env, execDetached, shellDir, screens, ...) and
# `Quickshell.Io`; see docs/plans/2026-10-30-delivery-plan.md.
set(quickshell_qml_files qml/PopupWindow.qml qml/Variants.qml)
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
  QML_FILES ${quickshell_qml_files}
  SOURCES qml/quickshell_types.h
)
target_include_directories(wabar_quickshell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qml)
target_link_libraries(wabar_quickshell
  PUBLIC Qt6::Quick Qt6::Qml wabar_host
  PRIVATE wabar_options)
wabar_qt_generated_code(wabar_quickshell)

add_subdirectory(wayland)
