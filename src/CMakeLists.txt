# wabar's product code by intent (docs/plans/2026-10-29-wabar-architecture-proposal.md, section 3).
# Include root is src/ (#include "features/tray/tray_host.h"); wabar_kernel carries it.
#
# Order: kernel, control, host, ui, features, modules, app. Features link the kernel, control and
# host. modules/ reads every contribution, so it runs after all contributors; it is included from
# the top-level CMakeLists.txt (see src/modules/qml_modules.cmake for why) right after this file,
# and so also after the legacy spike/ contributors. app only links targets, which CMake resolves
# at generate time. A unit folder that does not exist yet is skipped, so a move only has to
# create it.
foreach(unit IN ITEMS kernel control host ui features app)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${unit}/CMakeLists.txt")
    add_subdirectory(${unit})
  endif()
endforeach()
