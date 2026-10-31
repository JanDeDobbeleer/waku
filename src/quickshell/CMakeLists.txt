# quickshell: the Quickshell compatibility layer. Each folder is one QML module under Quickshell's
# URI, so `import Quickshell.Io` (and the others) in a shell written for Quickshell resolves; the
# types are wabar's own (host/qml/ and the features), registered again under Quickshell's names
# (QML_FOREIGN) or wrapped in QML. README.md lists every type and where it is implemented.
#
# Dependencies: the unit may include host/qml/, control/, kernel/ and the root and qml/ headers of
# any feature; only app/ and tests/ include it (rule S2). Added after features/ and before app/
# (src/CMakeLists.txt).
#
# wabar_quickshell_services (interface) carries the modules built by
# wabar_quickshell_service_module() and their static plugins; the executables and the tests link it.
add_library(wabar_quickshell_services INTERFACE)

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
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
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

add_subdirectory(io)
add_subdirectory(services/mpris)
add_subdirectory(services/notifications)
