# notifications: Action Center toasts, toast activation and Do Not Disturb (quiet hours).
# control_center reads Do Not Disturb through `DEPENDS notifications` (features/notifications/dnd.h).
wabar_feature(NAME notifications
  SOURCES
    dnd.h
    notifications.h
    notifications.cpp
    toast_activation.h
    toast_xml.h
    toast_xml.cpp
  WIN32_SOURCES
    win32/dnd_win.cpp
    win32/notifications_system.cpp
    win32/toast_activation_win.cpp
    win32/wnf_win.h
    win32/wnf_win.cpp
  QML_SOURCES
    qml/notification_feed.h
    qml/notification_feed.cpp
    qml/notification_service.h
    qml/notification_service.cpp
  QML_FILES
    qml/Notifications.qml
  TESTS
    tests/test_notification_feed.cpp
    tests/test_notifications.cpp
    tests/test_toast.cpp
    tests/test_wnf.cpp
  SYSTEM_LIBS ole32 uuid propsys shell32 advapi32 user32 windowsapp)
