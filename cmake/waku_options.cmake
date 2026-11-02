# waku_options: compile and link flags for our own targets. Applied PRIVATE,
# so third-party code keeps its warning level.
#
# Every own target links it PRIVATE: the libraries (kernel, control, features, host), the QML
# module targets (waku, waku.services, waku.widgets), the two executables and waku_tests.
# Define a new target's flags here, never per target.
#
add_library(waku_options INTERFACE)
option(WAKU_ANALYZE "Enable MSVC static analysis for own targets" OFF)
if(WAKU_ANALYZE)
  target_compile_options(waku_options INTERFACE /analyze /analyze:external-)
endif()
target_compile_features(waku_options INTERFACE cxx_std_20)
target_compile_definitions(waku_options INTERFACE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(waku_options INTERFACE
  /W4 /WX /permissive- /guard:cf /sdl /utf-8 /EHsc /Zc:__cplusplus
  /external:anglebrackets /external:W0)
target_link_options(waku_options INTERFACE /GUARD:CF /DYNAMICBASE /NXCOMPAT)

# waku_qt_generated_code(<qml module target>): the one place that relaxes a warning for code we do
# not write. The AOT output of qmlcachegen (<build>/.../.rcc/qmlcache/*.cpp) instantiates Qt
# templates and trips C4702 (unreachable code) inside QtCore/qvariant.h and QtQml/qjsengine.h;
# /external:W0 does not silence it because MSVC reports C4702 at the instantiation, after the
# header's external scope has ended. Only those generated files get /wd4702; the module's own
# sources keep /W4 /WX. Deferred because Qt adds the generated sources when the directory ends.
function(_waku_qt_generated_code_apply target)
  # The static plugin targets Qt creates for the module compile generated code too.
  foreach(plugin IN ITEMS ${target}plugin ${target}plugin_init)
    if(TARGET ${plugin})
      target_link_libraries(${plugin} PRIVATE waku_options)
    endif()
  endforeach()
  get_target_property(sources ${target} SOURCES)
  foreach(source IN LISTS sources)
    if(source MATCHES "/\.rcc/qmlcache/")
      set_source_files_properties(${source} TARGET_DIRECTORY ${target}
        PROPERTIES COMPILE_OPTIONS /wd4702)
    endif()
  endforeach()
endfunction()
function(waku_qt_generated_code target)
  # EVAL: a deferred call expands its variables when it runs, and ${target} is gone by then.
  cmake_language(EVAL CODE
    "cmake_language(DEFER CALL _waku_qt_generated_code_apply ${target})")
endfunction()
