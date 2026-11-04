# workspaces: komorebi and GlazeWM clients, the Windows virtual desktops and the shared Workspaces
# model.
# winhttp: the GlazeWM WebSocket client (win32/glazewm_socket.cpp); iphlpapi: its check that the
# port listens (GetExtendedTcpTable); runtimeobject: HSTRING for the shell desktop interface.
waku_feature(NAME workspaces
  SOURCES
    backoff.h
    command_line.h
    command_line.cpp
    desktop_command.h
    desktop_command.cpp
    event_bus.h
    glazewm.h
    glazewm_service.h
    glazewm_service.cpp
    glazewm_state.h
    glazewm_state.cpp
    komorebi.h
    komorebi_workarea.h
    komorebi_workarea.cpp
    komorebi_service.h
    komorebi_service.cpp
    komorebi_state.h
    komorebi_state.cpp
    virtual_desktops.h
    virtual_desktops.cpp
  WIN32_SOURCES
    win32/glazewm_socket.cpp
    win32/komorebi_pipe.cpp
    win32/komorebi_manage_win.cpp
    win32/komorebi_workarea_win.cpp
    win32/shell_desktops_win.h
    win32/shell_desktops_win.cpp
    win32/virtual_desktops_win.cpp
  QML_SOURCES
    qml/glazewm_client.h
    qml/glazewm_client.cpp
    qml/komorebi_client.h
    qml/komorebi_client.cpp
    qml/virtual_desktops_service.h
    qml/virtual_desktops_service.cpp
    qml/workspaces_service.h
    qml/workspaces_service.cpp
  QML_FILES
    qml/ActiveLayout.qml
    qml/WorkspaceDot.qml
    qml/Workspaces.qml
  TESTS
    tests/test_active_layout.cpp
    tests/test_backoff.cpp
    tests/test_command_line.cpp
    tests/test_desktop_command.cpp
    tests/test_glazewm_socket.cpp
    tests/test_glazewm_state.cpp
    tests/test_komorebi_manage.cpp
    tests/test_komorebi_service.cpp
    tests/test_komorebi_workarea.cpp
    tests/test_virtual_desktops.cpp
    tests/test_wm_clients.cpp
    tests/test_workspace_dot.cpp
    tests/test_workspaces.cpp
  SYSTEM_LIBS winhttp iphlpapi runtimeobject)
# The komorebi state is parsed JSON (komorebi_service.h, komorebi_state.h).
target_link_libraries(waku_workspaces PUBLIC nlohmann_json::nlohmann_json)
