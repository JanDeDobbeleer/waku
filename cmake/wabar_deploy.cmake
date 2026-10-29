# deploy: assembles a portable folder, dist/wabar by default (WABAR_DIST_DIR):
#
#   wabar.exe, wabarc.exe, WabarTrayHook[_arm64].dll   the program and the DLL injected into Explorer
#   <Qt runtime>                                        windeployqt, scanning src/ and the sample config
#   modules/wabar/ ...                                  the built-in widgets as a read-only reference:
#   modules/wabar/widgets/, modules/wabar/services/     .qml, qmldir and .qmltypes of the three QML
#                                                       modules (import path root: modules/)
#   examples/config/                                    the sample config home
#
# The built-in widgets stay compiled into wabar.exe; modules/ is only for reading, for copying a
# widget into a config home's widgets/ folder, and for qmllint/qmlls (pass -I <dist>/wabar/modules).
#
#   cmake --build --preset <preset> --target deploy
#
# Nothing here writes outside WABAR_DIST_DIR.
set(WABAR_DIST_DIR "${PROJECT_SOURCE_DIR}/dist/wabar" CACHE PATH "deploy output folder")
set(_dist "${WABAR_DIST_DIR}")
set(_modules "${_dist}/modules/wabar")

set(_windeployqt_flags $<IF:$<CONFIG:Debug>,--debug,--release>
  --no-translations --no-system-d3d-compiler --no-opengl-sw)

# QML module sources: wabar (host), wabar.widgets, wabar.services. The generated qmldir and
# .qmltypes come from the module targets' output folders.
file(GLOB _host_qml "${PROJECT_SOURCE_DIR}/src/host/qml/*.qml")
get_property(_widgets_qml GLOBAL PROPERTY WABAR_WIDGETS_QML_FILES)

set(_module_commands "")
foreach(entry IN ITEMS "wabar_host|${_modules}" "wabar_widgets|${_modules}/widgets"
                       "wabar_services|${_modules}/services")
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
          $<TARGET_FILE:wabar> $<TARGET_FILE:wabarc> $<TARGET_FILE:WabarTrayHook> "${_dist}"
  COMMAND Qt6::windeployqt ${_windeployqt_flags} --dir "${_dist}"
          --qmldir "${PROJECT_SOURCE_DIR}/src" "${_dist}/$<TARGET_FILE_NAME:wabar>"
  ${_module_commands}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_host_qml} "${_modules}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_widgets_qml} "${_modules}/widgets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/examples/config"
          "${_dist}/examples/config"
  DEPENDS wabar wabarc WabarTrayHook
  COMMENT "Deploying to ${_dist}"
  VERBATIM)
