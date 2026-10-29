# workspaces: komorebi and GlazeWM clients and the shared Workspaces model.
# winhttp: the GlazeWM WebSocket client (win32/glazewm_socket.cpp).
wabar_feature(NAME workspaces
  SOURCES
    command_line.h
    command_line.cpp
    event_bus.h
    glazewm.h
    glazewm_service.h
    glazewm_service.cpp
    glazewm_state.h
    glazewm_state.cpp
    komorebi.h
    komorebi_service.h
    komorebi_service.cpp
    komorebi_state.h
    komorebi_state.cpp
  WIN32_SOURCES
    win32/glazewm_socket.cpp
    win32/komorebi_pipe.cpp
  QML_SOURCES
    qml/glazewm_client.h
    qml/glazewm_client.cpp
    qml/komorebi_client.h
    qml/komorebi_client.cpp
    qml/workspaces_service.h
    qml/workspaces_service.cpp
  QML_FILES
    qml/ActiveLayout.qml
    qml/WorkspaceDot.qml
    qml/Workspaces.qml
  TESTS
    tests/test_active_layout.cpp
    tests/test_command_line.cpp
    tests/test_glazewm_socket.cpp
    tests/test_glazewm_state.cpp
    tests/test_komorebi_service.cpp
    tests/test_wm_clients.cpp
    tests/test_workspaces.cpp
  SYSTEM_LIBS winhttp)
# The komorebi state is parsed JSON (komorebi_service.h, komorebi_state.h).
target_link_libraries(wabar_workspaces PUBLIC nlohmann_json::nlohmann_json)
