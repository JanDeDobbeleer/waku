# workspaces: the Windows virtual desktops and the Workspaces model the bar shows.
# runtimeobject: HSTRING for the shell desktop interface.
waku_feature(NAME workspaces
  SOURCES
    desktop_command.h
    desktop_command.cpp
    virtual_desktops.h
    virtual_desktops.cpp
  WIN32_SOURCES
    win32/shell_desktops_win.h
    win32/shell_desktops_win.cpp
    win32/virtual_desktops_win.cpp
  QML_SOURCES
    qml/virtual_desktops_service.h
    qml/virtual_desktops_service.cpp
    qml/workspaces_service.h
    qml/workspaces_service.cpp
  QML_FILES
    qml/WorkspaceDot.qml
    qml/Workspaces.qml
  TESTS
    tests/test_desktop_command.cpp
    tests/test_virtual_desktops.cpp
    tests/test_workspace_dot.cpp
  SYSTEM_LIBS runtimeobject)
