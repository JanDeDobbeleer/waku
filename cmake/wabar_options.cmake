# wabar_options: compile and link flags for our own targets (was yasb_options). Applied PRIVATE,
# so third-party code keeps its warning level.
#
# Who links it (migration step 4): the legacy libraries that already linked yasb_options
# (yasb_platform, yasb_ui, yasb_widgets) and every target under src/ (wabar_feature() adds it).
# The former spike/ targets (wabar_bar, wabar_control, the two QML module targets, the exes and
# the test binary) do not link it yet: they set only the definitions below by hand. Step 9 links
# it there and fixes what /W4 /WX reveals.
#
# YASB_ANALYZE keeps its name until the rename step (10): tools\check.ps1 and docs/building.md
# pass it.
add_library(wabar_options INTERFACE)
option(YASB_ANALYZE "Enable MSVC static analysis for own targets" OFF)
if(YASB_ANALYZE)
  target_compile_options(wabar_options INTERFACE /analyze /analyze:external-)
endif()
target_compile_features(wabar_options INTERFACE cxx_std_20)
target_compile_definitions(wabar_options INTERFACE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(wabar_options INTERFACE
  /W4 /WX /permissive- /guard:cf /sdl /utf-8 /EHsc /Zc:__cplusplus
  /external:anglebrackets /external:W0)
target_link_options(wabar_options INTERFACE /GUARD:CF /DYNAMICBASE /NXCOMPAT)
