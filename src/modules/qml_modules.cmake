# Assembles the QML modules wabar.services and wabar.widgets from the contributions of every unit
# (wabar_feature() in src/features/*, wabar_contribute() in kernel, host, ui and app). Included by
# src/CMakeLists.txt after every contributor, so the global lists are complete here.
#
# Why include() from src/CMakeLists.txt and not add_subdirectory(src/modules): qt_add_qml_module names
# each qmlcachegen output after the .qml file's path relative to CMAKE_CURRENT_SOURCE_DIR
# (".rcc/qmlcache/<target>_<relative path>_qml.cpp", Qt 6.10 Qt6QmlMacros.cmake). From src/modules
# a feature file is "../features/<f>/qml/X.qml", which yields a directory "wabar_widgets_.." that
# Windows cannot create (trailing dots are stripped), and the build fails. The modules must be
# declared from a directory that contains every contributing file: src/.
#
# URIs, versions and resource paths are the user-facing API and stay as they were:
# qrc:/qt/qml/wabar/services/... and qrc:/qt/qml/wabar/widgets/<Name>.qml.
#
# QML files come from many folders. Each one gets QT_RESOURCE_ALIAS <basename>, set here because a
# source-file property is directory scoped and must be visible where qt_add_qml_module runs. The
# alias flattens the file into the module root, so .qml basenames must be unique per module (S7):
# a duplicate is a configure error below. NO_GENERATE_EXTRA_QMLDIRS because the extra qmldirs
# (QTP0004) ignore aliases. The build-directory copy of each module (qmldir, .qmltypes, the .qml
# files under their alias) lands in <build>/qml_modules/wabar/{services,widgets}: the folder
# follows the URI (QT_QML_OUTPUT_DIRECTORY, set in src/CMakeLists.txt), so qmlcachegen and qmllint
# resolve `import wabar.services as S` and the other modules.
get_property(services_sources GLOBAL PROPERTY WABAR_SERVICES_SOURCES)
get_property(services_include_dirs GLOBAL PROPERTY WABAR_SERVICES_INCLUDE_DIRS)
get_property(services_libs GLOBAL PROPERTY WABAR_SERVICES_LIBS)
get_property(widgets_qml_files GLOBAL PROPERTY WABAR_WIDGETS_QML_FILES)
list(REMOVE_DUPLICATES services_include_dirs)
list(REMOVE_DUPLICATES services_libs)

# Same rule for the QML-facing headers: the generated type registration includes them by base name.
set(seen_headers "")
foreach(file IN LISTS services_sources)
  if(file MATCHES "\\.h$")
    cmake_path(GET file FILENAME name)
    if(name IN_LIST seen_headers)
      message(FATAL_ERROR "wabar.services: header base name ${name} is not unique (${file}); "
        "rule S7")
    endif()
    list(APPEND seen_headers ${name})
  endif()
endforeach()

set(seen_qml "")
foreach(file IN LISTS widgets_qml_files)
  cmake_path(GET file FILENAME name)
  if(name IN_LIST seen_qml)
    message(FATAL_ERROR "wabar.widgets: ${name} is contributed twice (${file}); rule S7")
  endif()
  list(APPEND seen_qml ${name})
  set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

# wabar.services: the C++ half of the widget API.
qt_add_library(wabar_services STATIC)
qt_add_qml_module(wabar_services
  URI wabar.services
  VERSION 1.0
  NO_GENERATE_EXTRA_QMLDIRS
  SOURCES ${services_sources}
)
# src/ comes from wabar_kernel through the feature libraries.
target_include_directories(wabar_services PRIVATE ${services_include_dirs})
target_link_libraries(wabar_services
  PUBLIC Qt6::Qml Qt6::Quick ${services_libs}
  PRIVATE wabar_options)
wabar_qt_generated_code(wabar_services)

# wabar.widgets: the built-in widgets, plain QML on top of wabar.services. Inside the module the
# services are imported under the name S because the widgets share names with them (Cpu, Memory,
# Battery, Media, Workspaces).
qt_add_library(wabar_widgets STATIC)
qt_add_qml_module(wabar_widgets
  URI wabar.widgets
  VERSION 1.0
  NO_GENERATE_EXTRA_QMLDIRS
  QML_FILES ${widgets_qml_files}
)
target_link_libraries(wabar_widgets PUBLIC Qt6::Quick Qt6::Qml PRIVATE wabar_options wabar_host wabar_services)
wabar_qt_generated_code(wabar_widgets)
