# wabar's product code by intent (docs/plans/2026-10-29-wabar-architecture-proposal.md, section 3).
# Include root is src/ (#include "features/tray/tray_host.h"); wabar_kernel carries it.
#
# Order: kernel, control, host, ui, features, quickshell, app, then the QML modules. Features link
# the kernel, control and host; quickshell (the Quickshell compatibility modules) builds on host and
# the features. Every unit that contributes to the aggregated QML modules or to the test binary
# (wabar_feature(), wabar_contribute()) runs before modules/qml_modules.cmake, which reads the
# contributions; app contributes SafeShell.qml. The modules are included from here and not added
# as a subdirectory: see src/modules/qml_modules.cmake for why.
#
# Every QML module of src/ writes its build-directory copy (qmldir, .qmltypes, the .qml files) to
# <build>/qml_modules/<URI as path>: wabar -> qml_modules/wabar, wabar.services ->
# qml_modules/wabar/services, Quickshell.Io -> qml_modules/Quickshell/Io. qt_add_qml_module derives
# that folder from QT_QML_OUTPUT_DIRECTORY when a module names no OUTPUT_DIRECTORY, and passes the
# common root to qmlcachegen and qmllint as an import path, so both resolve `import wabar`,
# `import wabar.services` and `import Quickshell...` across modules. The folder is only for the
# tools and for deploy (cmake/wabar_deploy.cmake reads each module's output folder from its target):
# at run time the modules come from the static plugins and resources (qrc:/qt/qml/<URI>), and
# qml_modules is on no engine import path. It is not <build>/qml, which windeployqt fills with
# Qt's own modules for the executables in the build tree. The folder is created here because Qt adds
# QT_QML_OUTPUT_DIRECTORY to the import path only when it exists at configure time.
set(QT_QML_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/qml_modules)
file(MAKE_DIRECTORY ${QT_QML_OUTPUT_DIRECTORY})
foreach(unit IN ITEMS kernel control host ui features quickshell app)
  add_subdirectory(${unit})
endforeach()
include(modules/qml_modules.cmake)  # wabar.services, wabar.widgets
