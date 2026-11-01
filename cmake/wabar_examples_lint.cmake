# wabar_examples_qmllint: qmllint over the sample shells (examples/**/*.qml), part of all_qmllint
# and so of the qmllint step of tools\check.ps1. The examples are no QML module, so
# qt_add_qml_module makes no lint target for them; this one uses the same import paths as the
# module lint targets: the URI-shaped build copy of wabar's modules (QT_QML_OUTPUT_DIRECTORY,
# src/CMakeLists.txt) and Qt's own QML folder. A sample shell is linted as a user's config home is,
# against the module copies; `import "widgets"` resolves relative to the file.
file(GLOB_RECURSE _example_qml CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/examples/*.qml")
set(_example_lint_args --bare
  -I "${PROJECT_BINARY_DIR}/qml_modules"
  -I "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}"
  ${_example_qml})
set(_example_lint_rsp "${PROJECT_BINARY_DIR}/.rcc/qmllint/wabar_examples.rsp")
file(GENERATE OUTPUT "${_example_lint_rsp}" CONTENT "$<JOIN:${_example_lint_args},\n>\n")
add_custom_target(wabar_examples_qmllint
  COMMAND $<TARGET_FILE:Qt6::qmllint> "@${_example_lint_rsp}"
  DEPENDS Qt6::qmllint ${_example_qml} "${_example_lint_rsp}"
          $<TARGET_NAME_IF_EXISTS:all_qmltyperegistrations>
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  VERBATIM)
# The module lint targets also need the .qml copies in the module folders (composite types).
add_dependencies(wabar_examples_qmllint wabar_host wabar_services wabar_widgets)
if(TARGET all_qmllint)
  add_dependencies(all_qmllint wabar_examples_qmllint)
endif()
