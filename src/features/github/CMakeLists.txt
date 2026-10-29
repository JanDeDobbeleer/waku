# GitHub notifications and device-flow sign-in. The root is Qt Core and Qt Network; the QML service
# and widget use shell for the "open URL" clicks. test_github.cpp stays in spike/tests until the bar
# fixtures move to host/testing.
wabar_feature(NAME github
  DEPENDS shell
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
  SYSTEM_LIBS Qt6::Network)
