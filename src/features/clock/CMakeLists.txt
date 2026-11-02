# clock: date and time in the bar, with a calendar popup. QML only.
waku_feature(NAME clock
  QML_FILES
    qml/Calendar.qml
    qml/Clock.qml
  TESTS
    tests/test_clock.cpp)
