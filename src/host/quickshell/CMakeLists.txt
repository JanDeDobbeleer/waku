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

# wabar_quickshell_qml_module(<target> <uri>
#     [QML_FILES ...] [SINGLETONS ...] [INTERNAL ...] [SOURCES ...] [DEPENDENCIES ...])
# One more Quickshell module made of QML files (and optionally C++ in qml/), linked into
# wabar_quickshell so the executables and the tests get it without naming it. Call it from the
# module's own folder. SINGLETONS are `pragma Singleton` files, INTERNAL ones are not exported.
# The QML imports wabar and wabar.services at run time; DEPENDENCIES only declares that.
function(wabar_quickshell_qml_module target uri)
  cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "QML_FILES;SINGLETONS;INTERNAL;SOURCES;DEPENDENCIES")
  set(all ${arg_QML_FILES} ${arg_SINGLETONS} ${arg_INTERNAL})
  foreach(file IN LISTS all)
    cmake_path(GET file FILENAME name)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
  endforeach()
  set_source_files_properties(${arg_SINGLETONS} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
  if(arg_INTERNAL)
    set_source_files_properties(${arg_INTERNAL} PROPERTIES QT_QML_INTERNAL_TYPE TRUE)
  endif()
  qt_add_library(${target} STATIC)
  qt_add_qml_module(${target}
    URI ${uri}
    VERSION 0.1
    NO_GENERATE_EXTRA_QMLDIRS
    QML_FILES ${all}
    SOURCES ${arg_SOURCES}
    DEPENDENCIES ${arg_DEPENDENCIES}
  )
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qml)
  target_link_libraries(${target}
    PUBLIC Qt6::Quick Qt6::Qml wabar_host
    PRIVATE wabar_options)
  wabar_qt_generated_code(${target})
  target_link_libraries(wabar_quickshell PUBLIC ${target} ${target}plugin)
endfunction()

add_subdirectory(wayland)
add_subdirectory(widgets)
add_subdirectory(hyprland)
add_subdirectory(upower)
add_subdirectory(pipewire)
add_subdirectory(systemtray)
add_subdirectory(bluetooth)
