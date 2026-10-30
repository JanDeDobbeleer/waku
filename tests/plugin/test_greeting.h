#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

// The one type of the test plugin `WabarTestModule` (tests/plugin/CMakeLists.txt): a user-built Qt
// QML plugin that the import-path tests load from a temporary modules folder.
class TestGreeting : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString text READ text CONSTANT FINAL)

 public:
  explicit TestGreeting(QObject* parent = nullptr) : QObject(parent) {}
  QString text() const { return QStringLiteral("from the plugin"); }
};
