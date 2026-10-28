#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

class QQmlEngine;

namespace spike {

// The user widget list, rescanned and hot reloaded when a file in the widget folder changes.
class Host : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList widgetFiles READ widgetFiles NOTIFY widgetFilesChanged)

 public:
  Host(QQmlEngine* engine, const QString& widgetsDir, QObject* parent = nullptr);

  QStringList widgetFiles() const { return widgetFiles_; }

 signals:
  void widgetFilesChanged();

 private:
  void rescan();

  QQmlEngine* engine_;
  QString dir_;
  QFileSystemWatcher watcher_;
  QTimer debounce_;
  QStringList widgetFiles_;
};

}  // namespace spike
