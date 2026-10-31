# Quickshell.Io and the Quickshell singleton's window-free members, so QML written for Quickshell
# loads: Process, StdioCollector, SplitParser, FileView, JsonAdapter, JsonObject, FileViewError,
# ElapsedTimer and QuickshellSingleton. See design.md.
wabar_feature(NAME quickshell_io
  SOURCES
    command_spec.h
    command_spec.cpp
    file_access.h
    file_access.cpp
    process_job.h
  WIN32_SOURCES
    win32/process_job_win.cpp
  QML_SOURCES
    qml/clipboard_text.h
    qml/clipboard_text.cpp
    qml/data_stream_parsers.h
    qml/data_stream_parsers.cpp
    qml/file_view.h
    qml/file_view.cpp
    qml/json_adapter.h
    qml/json_adapter.cpp
    qml/qs_process.h
    qml/qs_process.cpp
    qml/quickshell_singleton.h
    qml/quickshell_singleton.cpp
  TESTS
    tests/test_quickshell_file_view.cpp
    tests/test_quickshell_process.cpp
    tests/test_quickshell_singleton.cpp)
