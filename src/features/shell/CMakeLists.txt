# Run programs and open targets from QML (Shell, Process). QML only; the Windows launch helpers
# come from kernel.
wabar_feature(NAME shell
  QML_SOURCES
    qml/child_process.h
    qml/child_process.cpp
    qml/shell_service.h
    qml/shell_service.cpp
  TESTS
    tests/test_process.cpp
    tests/test_shell.cpp)
