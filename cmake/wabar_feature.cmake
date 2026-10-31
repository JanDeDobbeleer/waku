# wabar_feature(): one feature's declaration (docs/plans/2026-10-29-wabar-architecture-proposal.md,
# section 7.2), and wabar_contribute(), the lower-level call it and the other units (kernel, host,
# ui, app) use to feed the two aggregated QML modules and the test binary.
#
#   wabar_feature(NAME <f>
#     DEPENDS       <features...>   features whose root headers and QML types <f> may use
#     SOURCES       <files...>      feature root: domain, interfaces, UI-free logic
#     WIN32_SOURCES <files...>      win32/: the Windows implementation
#     QML_SOURCES   <files...>      qml/*.h, qml/*.cpp: QML-facing C++, compiled into wabar.services
#     QML_FILES     <files...>      qml/*.qml: widgets, compiled into wabar.widgets
#     TESTS         <files...>      tests/*.cpp: compiled into the one test binary
#     SYSTEM_LIBS   <libs...>       Windows import libraries the feature calls
#     WINRT_PCH     <headers...>    C++/WinRT headers (winrt/base.h, ...) precompiled once for the
#                                   WIN32_SOURCES; the root SOURCES do not use the PCH
#     SUBTARGET_SOURCES <files...>) sources of a target the feature builds in its own subfolder
#                                   (tray/hook, the DLL): listed only so rule S1 sees them; they
#                                   are not compiled into wabar_<f>
#
# Decisions the proposal left open (keep them simple; revisit in step 11 if they chafe):
# - Argument names follow the step-4 brief (QML_SOURCES/QML_FILES, the qt_add_qml_module words)
#   instead of the proposal's QML_TYPES/QML_WIDGETS; the feature is named with NAME.
# - wabar_<f> is STATIC when SOURCES or WIN32_SOURCES is given, else INTERFACE (QML-only features
#   such as clock), so a DEPENDS on any feature always links.
# - wabar_<f> links PUBLIC wabar_kernel and wabar_<d> for each DEPENDS, PRIVATE wabar_options and
#   SYSTEM_LIBS. The QML-facing C++ is compiled in the module target, which links wabar_options too.
# - DEPENDS must name a feature declared earlier in src/features/CMakeLists.txt (configure error
#   otherwise). Declaration order is thus a topological order and a cycle cannot configure (S8).
# - Paths are checked against the inner layout: WIN32_SOURCES under win32/, QML_* under qml/, TESTS
#   under tests/, SOURCES in none of win32/ qml/ tests/ testing/. A wrong folder is a configure
#   error, so S1 holds for CMake as well as for tools\check.ps1.
# - Contributions are global properties holding absolute paths:
#     WABAR_SERVICES_SOURCES, WABAR_SERVICES_INCLUDE_DIRS, WABAR_SERVICES_LIBS  -> wabar.services
#     WABAR_WIDGETS_QML_FILES                                                  -> wabar.widgets
#     WABAR_TEST_SOURCES, WABAR_TEST_LIBS                                      -> wabar_tests
#   src/modules/ reads the first two groups after every contributor ran; tests/ reads the last.
#   The directory of each QML_SOURCES header goes to WABAR_SERVICES_INCLUDE_DIRS because the
#   generated type registration includes headers by base name (hence rule S7).
# - WABAR_FEATURES lists the features in declaration order and WABAR_FEATURE_<f>_DEPENDS their
#   DEPENDS, for anything that wants the graph at configure time. tools\check.ps1 does not use
#   them: it parses the wabar_feature( calls itself, so it runs without a build tree.
include_guard(GLOBAL)

# Appends absolute paths (relative ones resolve against the calling CMakeLists.txt) and plain
# target names to the global contribution lists.
function(wabar_contribute)
  cmake_parse_arguments(PARSE_ARGV 0 arg ""
    "" "SERVICES_SOURCES;SERVICES_INCLUDE_DIRS;SERVICES_LIBS;WIDGETS_QML_FILES;TESTS;TEST_LIBS")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "wabar_contribute: unknown arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()
  foreach(kind SERVICES_SOURCES SERVICES_INCLUDE_DIRS WIDGETS_QML_FILES TESTS)
    set(paths "")
    foreach(item IN LISTS arg_${kind})
      cmake_path(ABSOLUTE_PATH item BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE
        OUTPUT_VARIABLE path)
      list(APPEND paths "${path}")
    endforeach()
    if(kind STREQUAL "TESTS")
      set(property WABAR_TEST_SOURCES)
    else()
      set(property WABAR_${kind})
    endif()
    set_property(GLOBAL APPEND PROPERTY ${property} ${paths})
  endforeach()
  set_property(GLOBAL APPEND PROPERTY WABAR_SERVICES_LIBS ${arg_SERVICES_LIBS})
  set_property(GLOBAL APPEND PROPERTY WABAR_TEST_LIBS ${arg_TEST_LIBS})
endfunction()

# Fails configure unless every path in <list> matches <regex> (relative to the feature folder).
function(_wabar_feature_check_paths feature kind regex)
  foreach(path IN LISTS ARGN)
    if(NOT path MATCHES "${regex}")
      message(FATAL_ERROR
        "wabar_feature(${feature}): ${kind} entry '${path}' must match ${regex} "
        "(see the inner layout in docs/architecture.md)")
    endif()
  endforeach()
endfunction()

function(wabar_feature)
  cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME"
    "DEPENDS;SOURCES;WIN32_SOURCES;QML_SOURCES;QML_FILES;TESTS;SYSTEM_LIBS;SUBTARGET_SOURCES;WINRT_PCH")
  set(f "${arg_NAME}")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "wabar_feature(${f}): unknown arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT f MATCHES "^[a-z][a-z0-9_]*$")
    message(FATAL_ERROR "wabar_feature: NAME '${f}' must be lower snake case")
  endif()
  if(TARGET wabar_${f})
    message(FATAL_ERROR "wabar_feature(${f}): target wabar_${f} already exists")
  endif()

  foreach(path IN LISTS arg_SOURCES)
    if(path MATCHES "^(win32|qml|tests|testing)/")
      message(FATAL_ERROR "wabar_feature(${f}): SOURCES entry '${path}' belongs in "
        "WIN32_SOURCES, QML_SOURCES or TESTS (testing/ is header-only)")
    endif()
  endforeach()
  _wabar_feature_check_paths(${f} WIN32_SOURCES "^win32/" ${arg_WIN32_SOURCES})
  _wabar_feature_check_paths(${f} QML_SOURCES "^qml/[^/]+\\.(h|cpp)$" ${arg_QML_SOURCES})
  _wabar_feature_check_paths(${f} QML_FILES "^qml/[^/]+\\.qml$" ${arg_QML_FILES})
  _wabar_feature_check_paths(${f} TESTS "^tests/" ${arg_TESTS})

  set(dep_targets "")
  foreach(d IN LISTS arg_DEPENDS)
    if(d STREQUAL f OR NOT TARGET wabar_${d})
      message(FATAL_ERROR
        "wabar_feature(${f}): DEPENDS ${d} is not a feature declared before ${f} in "
        "src/features/CMakeLists.txt")
    endif()
    list(APPEND dep_targets wabar_${d})
  endforeach()

  if(arg_SOURCES OR arg_WIN32_SOURCES)
    add_library(wabar_${f} STATIC ${arg_SOURCES} ${arg_WIN32_SOURCES})
    target_link_libraries(wabar_${f}
      PUBLIC wabar_kernel ${dep_targets}
      PRIVATE wabar_options ${arg_SYSTEM_LIBS})
  else()
    if(arg_SYSTEM_LIBS)
      message(FATAL_ERROR "wabar_feature(${f}): SYSTEM_LIBS without SOURCES or WIN32_SOURCES")
    endif()
    add_library(wabar_${f} INTERFACE)
    target_link_libraries(wabar_${f} INTERFACE wabar_kernel ${dep_targets})
  endif()

  if(arg_WINRT_PCH)
    if(NOT arg_WIN32_SOURCES)
      message(FATAL_ERROR "wabar_feature(${f}): WINRT_PCH without WIN32_SOURCES")
    endif()
    # unknwn.h first: C++/WinRT turns on its classic COM support (com_ptr::as, implements of a
    # COM interface) only when IUnknown is declared before winrt/base.h.
    set(pch_headers <unknwn.h>)
    foreach(header IN LISTS arg_WINRT_PCH)
      list(APPEND pch_headers <${header}>)
    endforeach()
    target_precompile_headers(wabar_${f} PRIVATE ${pch_headers})
    # The root sources never see the Windows headers (rule S3), so they skip the PCH.
    if(arg_SOURCES)
      set_source_files_properties(${arg_SOURCES} TARGET_DIRECTORY wabar_${f}
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endif()
  endif()

  set(services_libs "")
  set(include_dirs "")
  if(arg_QML_SOURCES)
    set(services_libs wabar_${f})
    set(include_dirs qml)
  endif()
  set(test_libs "")
  if(arg_TESTS)
    set(test_libs wabar_${f})
  endif()
  wabar_contribute(
    SERVICES_SOURCES ${arg_QML_SOURCES}
    SERVICES_INCLUDE_DIRS ${include_dirs}
    SERVICES_LIBS ${services_libs}
    WIDGETS_QML_FILES ${arg_QML_FILES}
    TESTS ${arg_TESTS}
    TEST_LIBS ${test_libs})

  set_property(GLOBAL APPEND PROPERTY WABAR_FEATURES ${f})
  set_property(GLOBAL PROPERTY WABAR_FEATURE_${f}_DEPENDS ${arg_DEPENDS})
endfunction()
