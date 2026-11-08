# Assembles the QML modules waku.services and waku.widgets from the contributions of every unit
# (waku_feature() in src/features/*, waku_contribute() in kernel, host, ui and app). Included by
# src/CMakeLists.txt after every contributor, so the global lists are complete here.
#
# Why include() from src/CMakeLists.txt and not add_subdirectory(src/modules): qt_add_qml_module names
# each qmlcachegen output after the .qml file's path relative to CMAKE_CURRENT_SOURCE_DIR
# (".rcc/qmlcache/<target>_<relative path>_qml.cpp", Qt 6.12 Qt6QmlMacros.cmake). From src/modules
# a feature file is "../features/<f>/qml/X.qml", which yields a directory "waku_widgets_.." that
# Windows cannot create (trailing dots are stripped), and the build fails. The modules must be
# declared from a directory that contains every contributing file: src/.
#
# URIs, versions and resource paths are the user-facing API and stay as they were:
# qrc:/qt/qml/waku/services/... and qrc:/qt/qml/waku/widgets/<Name>.qml.
#
# QML files come from many folders. Each one gets QT_RESOURCE_ALIAS <basename>, set here because a
# source-file property is directory scoped and must be visible where qt_add_qml_module runs. The
# alias flattens the file into the module root, so .qml basenames must be unique per module (S7):
# a duplicate is a configure error below. NO_GENERATE_EXTRA_QMLDIRS because the extra qmldirs
# (QTP0004) ignore aliases. The build-directory copy of each module (qmldir, .qmltypes, the .qml
# files under their alias) lands in <build>/qml_modules/waku/{services,widgets}: the folder
# follows the URI (QT_QML_OUTPUT_DIRECTORY, set in src/CMakeLists.txt), so qmlcachegen and qmllint
# resolve `import waku.services as S` and the other modules.
get_property(services_sources GLOBAL PROPERTY WAKU_SERVICES_SOURCES)
get_property(services_include_dirs GLOBAL PROPERTY WAKU_SERVICES_INCLUDE_DIRS)
get_property(services_libs GLOBAL PROPERTY WAKU_SERVICES_LIBS)
get_property(widgets_qml_files GLOBAL PROPERTY WAKU_WIDGETS_QML_FILES)
list(REMOVE_DUPLICATES services_include_dirs)
list(REMOVE_DUPLICATES services_libs)

# Same rule for the QML-facing headers: the generated type registration includes them by base name.
set(seen_headers "")
foreach(file IN LISTS services_sources)
  if(file MATCHES "\\.h$")
    cmake_path(GET file FILENAME name)
    if(name IN_LIST seen_headers)
      message(FATAL_ERROR "waku.services: header base name ${name} is not unique (${file}); "
        "rule S7")
    endif()
    list(APPEND seen_headers ${name})
  endif()
endforeach()

set(seen_qml "")
foreach(file IN LISTS widgets_qml_files)
  cmake_path(GET file FILENAME name)
  if(name IN_LIST seen_qml)
    message(FATAL_ERROR "waku.widgets: ${name} is contributed twice (${file}); rule S7")
  endif()
  list(APPEND seen_qml ${name})
  set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

# waku.services: the C++ half of the widget API.
qt_add_library(waku_services STATIC)
qt_add_qml_module(waku_services
  URI waku.services
  VERSION 1.0
  NO_GENERATE_EXTRA_QMLDIRS
  SOURCES ${services_sources}
)
# src/ comes from waku_kernel through the feature libraries.
target_include_directories(waku_services PRIVATE ${services_include_dirs})
target_link_libraries(waku_services
  PUBLIC Qt6::Qml Qt6::Quick ${services_libs}
  PRIVATE waku_options)
waku_qt_generated_code(waku_services)

# waku.widgets: the built-in widgets, plain QML on top of waku.services. Inside the module the
# services are imported under the name S because the widgets share names with them (Cpu, Memory,
# Battery, Media, Workspaces).
qt_add_library(waku_widgets STATIC)
qt_add_qml_module(waku_widgets
  URI waku.widgets
  VERSION 1.0
  NO_GENERATE_EXTRA_QMLDIRS
  QML_FILES ${widgets_qml_files}
)
target_link_libraries(waku_widgets PUBLIC Qt6::Quick Qt6::Qml PRIVATE waku_options waku_host waku_services)
waku_qt_generated_code(waku_widgets)
