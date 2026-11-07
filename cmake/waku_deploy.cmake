# deploy: assembles a portable folder, dist/waku by default (WAKU_DIST_DIR):
#
#   waku.exe, wakuc.exe, wakukeys.exe, WakuTrayHook[_arm64].dll
#                                                       the bar, its console client, the hotkey
#                                                       daemon and the DLL injected into Explorer
#   <Qt runtime>                                        windeployqt, scanning src/ and cmake/deploy_imports/
#   modules/waku/ ...                                  the built-in widgets as a read-only reference:
#   modules/waku/widgets/, modules/waku/services/     .qml, qmldir and .qmltypes of the three QML
#                                                       modules (import path root: modules/)
#   examples/config/                                    the sample config home
#
# The built-in widgets stay compiled into waku.exe; modules/ is only for reading, for copying a
# widget into a config home's widgets/ folder, and for qmllint/qmlls (pass -I <dist>/waku/modules).
#
#   cmake --build --preset <preset> --target deploy
#
# Nothing here writes outside WAKU_DIST_DIR.
set(WAKU_DIST_DIR "${PROJECT_SOURCE_DIR}/dist/waku" CACHE PATH "deploy output folder")
set(_dist "${WAKU_DIST_DIR}")
set(_modules "${_dist}/modules/waku")

# Image formats: windeployqt takes every imageformats plugin it finds (qgif, qico, qjpeg, qsvg and,
# from the Qt Image Formats add-on, qwebp). Shells load .webp wallpapers, so qwebp is required; the
# add-on's other formats (qicns, qtga, qtiff, qwbmp) stay out.
set(_windeployqt_flags $<IF:$<CONFIG:Debug>,--debug,--release>
  --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw
  --include-plugins qwebp --exclude-plugins qicns,qtga,qtiff,qwbmp)
if(NOT EXISTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/imageformats/qwebp.dll")
  message(WARNING "The qwebp image plugin is missing from Qt (${QT6_INSTALL_PREFIX}): install the "
                  "Qt Image Formats module (qtimageformats), or .webp images will not load.")
endif()

# QML module sources: waku (host), waku.widgets, waku.services. The generated qmldir and
# .qmltypes come from the module targets' output folders.
file(GLOB _host_qml "${PROJECT_SOURCE_DIR}/src/host/qml/*.qml")
get_property(_widgets_qml GLOBAL PROPERTY WAKU_WIDGETS_QML_FILES)

set(_module_commands "")
foreach(entry IN ITEMS "waku_host|${_modules}" "waku_widgets|${_modules}/widgets"
                       "waku_services|${_modules}/services")
  string(REPLACE "|" ";" entry "${entry}")
  list(GET entry 0 _target)
  list(GET entry 1 _dest)
  get_target_property(_out ${_target} QT_QML_MODULE_OUTPUT_DIRECTORY)
  get_target_property(_typeinfo ${_target} QT_QML_MODULE_TYPEINFO)
  list(APPEND _module_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_out}/qmldir" "${_out}/${_typeinfo}" "${_dest}")
endforeach()

add_custom_target(deploy
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_dist}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:waku> $<TARGET_FILE:wakuc> $<TARGET_FILE:wakukeys>
          $<TARGET_FILE:WakuTrayHook> "${_dist}"
  COMMAND Qt6::windeployqt ${_windeployqt_flags} --dir "${_dist}"
          --qmldir "${PROJECT_SOURCE_DIR}/src"
          --qmldir "${PROJECT_SOURCE_DIR}/cmake/deploy_imports" "${_dist}/$<TARGET_FILE_NAME:waku>"
  ${_module_commands}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_host_qml} "${_modules}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_widgets_qml} "${_modules}/widgets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/examples/config"
          "${_dist}/examples/config"
  DEPENDS waku wakuc wakukeys WakuTrayHook
  COMMENT "Deploying to ${_dist}"
  VERBATIM)
