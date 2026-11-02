# GitHub notifications and device-flow sign-in. The root is Qt Core and Qt Network; the QML service
# and widget use Shell (quickshell_io) for the "open URL" clicks. The widget tests use the bar
# Harness of host/testing.
waku_feature(NAME github
  DEPENDS quickshell_io
  SOURCES
    github_api.cpp
    github_api.h
    github_format.cpp
    github_format.h
    github_icons.h
  QML_SOURCES
    qml/github_service.cpp
    qml/github_service.h
  QML_FILES
    qml/GitHub.qml
  TESTS
    tests/test_github_api.cpp
    tests/test_github.cpp
  SYSTEM_LIBS Qt6::Network)
