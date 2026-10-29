# notifications: Action Center toasts, toast activation and Do Not Disturb (quiet hours).
# control_center reads Do Not Disturb through `DEPENDS notifications` (features/notifications/dnd.h).
# win32/toast_activation_win.cpp still takes activateAppByAumid from platform/media_session.h; the
# merge switches it to the kernel app activation helper (and drops the yasb_platform link below).
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
  QML_SOURCES
    qml/notification_service.h
    qml/notification_service.cpp
  QML_FILES
    qml/Notifications.qml
  TESTS
    tests/test_notifications.cpp
    tests/test_toast.cpp
  SYSTEM_LIBS ole32 uuid propsys shell32 advapi32 user32 windowsapp)
# TEMPORARY: activateAppByAumid still lives in the legacy platform library.
target_link_libraries(wabar_notifications PRIVATE yasb_platform)
