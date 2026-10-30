# WabarTestModule: a tiny shared Qt QML plugin, built only for wabar_tests. It stands for the
# plugin a user builds and drops into <config home>/modules. The tests copy it (qmldir, library,
# typeinfo) from <build>/testmodules/WabarTestModule into a temporary modules folder.
#
# Not part of ALL: wabar_tests depends on it (tests/CMakeLists.txt).
qt_add_qml_module(wabar_test_module
  URI WabarTestModule
  VERSION 1.0
  SHARED
  PLUGIN_TARGET wabar_test_module
  NO_GENERATE_EXTRA_QMLDIRS
  OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/testmodules/WabarTestModule
  SOURCES test_greeting.h
)
target_link_libraries(wabar_test_module PRIVATE Qt6::Qml)
set_target_properties(wabar_test_module PROPERTIES EXCLUDE_FROM_ALL TRUE)
