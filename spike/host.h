#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include "platform/memory.h"

class QQmlEngine;

namespace spike {

// Everything QML sees of the host: the user widget list (hot reloaded) and one data source.
class Host : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList widgetFiles READ widgetFiles NOTIFY widgetFilesChanged)
  Q_PROPERTY(double memoryPercent READ memoryPercent NOTIFY memoryChanged)

 public:
  Host(QQmlEngine* engine, const QString& widgetsDir, QObject* parent = nullptr);

  QStringList widgetFiles() const { return widgetFiles_; }
  double memoryPercent() const { return memoryPercent_; }

 signals:
  void widgetFilesChanged();
  void memoryChanged();

 private:
  void rescan();
  void sampleMemory();

  QQmlEngine* engine_;
  QString dir_;
  QFileSystemWatcher watcher_;
  QTimer debounce_;
  QTimer memoryTimer_;
  QStringList widgetFiles_;
  yasb::platform::WindowsMemoryDataSource memory_;
  double memoryPercent_ = 0;
};

}  // namespace spike
