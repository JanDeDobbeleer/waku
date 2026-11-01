# quickshell: the Quickshell compatibility layer. Each folder is one QML module under Quickshell's
# URI, so `import Quickshell.Io` (and the others) in a shell written for Quickshell resolves; the
# types are wabar's own (host/qml/ and the features), registered again under Quickshell's names
# (QML_FOREIGN) or wrapped in QML. README.md lists every type and where it is implemented.
#
# Dependencies: the unit may include host/qml/, control/, kernel/ and the root and qml/ headers of
# any feature; only app/ and tests/ include it (rule S2). Added after features/ and before app/
# (src/CMakeLists.txt).
#
# core/ builds wabar_quickshell (module `Quickshell`), and every module made with
# wabar_quickshell_qml_module() links into it. wabar_quickshell_services (interface) carries the
# modules built by wabar_quickshell_service_module() and their static plugins.
# wabar_quickshell_modules (interface) carries all of them: the executables and the tests link it
# and nothing else of this unit.
add_library(wabar_quickshell_services INTERFACE)
add_library(wabar_quickshell_modules INTERFACE)
target_link_libraries(wabar_quickshell_modules INTERFACE
  wabar_quickshell wabar_quickshellplugin wabar_quickshell_services)

# wabar_quickshell_service_module(<target> <uri> <header>)
# A module made of one header of QML_FOREIGN registrations (in qml/ of the calling folder) of
# feature types that live in wabar.services. The module depends on wabar.services but must not
# re-export it (a qmldir `import` line would): then `import Quickshell.Io` would show every
# wabar.services name, and a shell's own Wallpapers.qml or Workspaces.qml would lose to wabar's
# service of the same name. It links wabar_services, which holds the staticMetaObject of the types
# it registers.
function(wabar_quickshell_service_module target uri header)
  qt_add_library(${target} STATIC)
  qt_add_qml_module(${target}
    URI ${uri}
    VERSION 1.0
    NO_GENERATE_EXTRA_QMLDIRS
    DEPENDENCIES wabar.services
    SOURCES qml/${header}
  )
  # The generated registration includes the header by its base name.
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qml)
  target_link_libraries(${target}
    PUBLIC Qt6::Qml Qt6::Quick wabar_kernel
    PRIVATE wabar_options wabar_services)
  wabar_qt_generated_code(${target})
  target_link_libraries(wabar_quickshell_services INTERFACE ${target} ${target}plugin)
endfunction()

# wabar_quickshell_qml_module(<target> <uri>
#     [QML_FILES ...] [SINGLETONS ...] [INTERNAL ...] [SOURCES ...] [DEPENDENCIES ...])
# One more Quickshell module made of QML files (and optionally C++ in qml/), linked into
# wabar_quickshell (core/) so the executables and the tests get it without naming it. Call it
# from the module's own folder, after core/. SINGLETONS are `pragma Singleton` files, INTERNAL
# ones are not exported.
# The QML imports wabar and wabar.services at run time; DEPENDENCIES only declares that. A module
# of C++ registrations only (Quickshell.Wayland) passes SOURCES alone.
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
    PRIVATE wabar_options wabar_services)
  wabar_qt_generated_code(${target})
  target_link_libraries(wabar_quickshell PUBLIC ${target} ${target}plugin)
endfunction()

add_subdirectory(core)
add_subdirectory(wayland)
add_subdirectory(widgets)
add_subdirectory(hyprland)
add_subdirectory(bluetooth)
add_subdirectory(networking)
add_subdirectory(io)
add_subdirectory(services/mpris)
add_subdirectory(services/notifications)
add_subdirectory(services/upower)
add_subdirectory(services/pipewire)
add_subdirectory(services/systemtray)

# The tests load the modules through `import Quickshell...` in the bar Harness (host/testing/).
wabar_contribute(
  TESTS
    tests/test_quickshell.cpp
    tests/test_quickshell_core.cpp
    tests/test_quickshell_items.cpp
    tests/test_quickshell_modules.cpp
)
