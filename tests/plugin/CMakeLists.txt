# WakuTestModule: a tiny shared Qt QML plugin, built only for waku_tests. It stands for the
# plugin a user builds and drops into <config home>/modules. The tests copy it (qmldir, library,
# typeinfo) from <build>/testmodules/WakuTestModule into a temporary modules folder.
#
# Not part of ALL: waku_tests depends on it (tests/CMakeLists.txt).
qt_add_qml_module(waku_test_module
  URI WakuTestModule
  VERSION 1.0
  SHARED
  PLUGIN_TARGET waku_test_module
  NO_GENERATE_EXTRA_QMLDIRS
  OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/testmodules/WakuTestModule
  SOURCES test_greeting.h
)
target_link_libraries(waku_test_module PRIVATE Qt6::Qml)
set_target_properties(waku_test_module PROPERTIES EXCLUDE_FROM_ALL TRUE)
