# wabar's product code by intent (docs/plans/2026-10-29-wabar-architecture-proposal.md, section 3).
# Include root is src/ (#include "features/tray/tray_host.h"); wabar_kernel carries it.
#
# Order: kernel, control, host, ui, features, quickshell, app, then the QML modules. Features link
# the kernel, control and host; quickshell (the Quickshell compatibility modules) builds on host and
# the features. Every unit that contributes to the aggregated QML modules or to the test binary
# (wabar_feature(), wabar_contribute()) runs before modules/qml_modules.cmake, which reads the
# contributions; app contributes SafeShell.qml. The modules are included from here and not added
# as a subdirectory: see src/modules/qml_modules.cmake for why.
foreach(unit IN ITEMS kernel control host ui features quickshell app)
  add_subdirectory(${unit})
endforeach()
include(modules/qml_modules.cmake)  # wabar.services, wabar.widgets
