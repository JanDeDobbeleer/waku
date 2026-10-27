#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <functional>

namespace yasb::app {

class FileWatcher : public QObject {
  Q_OBJECT

 public:
  using Validator = std::function<bool()>;
  using Callback = std::function<void()>;

  FileWatcher(QString configPath, QString stylesheetPath, Validator configIsValid,
              Callback configChanged, Callback stylesheetChanged, int debounceMs = 250,
              bool watchConfig = true, bool watchStylesheet = true, QObject* parent = nullptr);

 private:
  struct Stamp {
    bool exists = false;
    qint64 size = -1;
    QDateTime modified;
    bool operator==(const Stamp&) const = default;
  };

  static Stamp stamp(const QString& path);
  void inspectChanges();
  void rearm();
  void flush();

  QString configPath_;
  QString stylesheetPath_;
  QString directory_;
  QFileSystemWatcher watcher_;
  QTimer timer_;
  Validator configIsValid_;
  Callback configChanged_;
  Callback stylesheetChanged_;
  Stamp configStamp_;
  Stamp stylesheetStamp_;
  bool configPending_ = false;
  bool stylesheetPending_ = false;
  bool watchConfig_ = true;
  bool watchStylesheet_ = true;
};

}  // namespace yasb::app
