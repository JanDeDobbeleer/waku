# Spike: Qt Quick top bar with QML widgets. Throwaway; checks app bar + backdrop on a
# QQuickWindow, memory footprint and hot reload of user widgets.
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick Qml)

qt_add_executable(yasb-qml-spike WIN32
  main.cpp
  host.h
  host.cpp
)
qt_add_qml_module(yasb-qml-spike
  URI YasbSpike
  VERSION 1.0
  QML_FILES
    qml/Bar.qml
    qml/Clock.qml
    qml/MemoryWidget.qml
)
target_compile_definitions(yasb-qml-spike PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  SPIKE_WIDGETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/widgets")
target_link_libraries(yasb-qml-spike PRIVATE
  Qt6::Quick Qt6::Qml yasb_ui yasb_platform)
set_target_properties(yasb-qml-spike PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
