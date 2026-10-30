#include "host/taskbar_control.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

namespace wabar::host {

QString taskbarStateName(TaskbarState state) {
  switch (state) {
    case TaskbarState::Hidden:
      return QStringLiteral("hidden");
    case TaskbarState::AutoHide:
      return QStringLiteral("autohide-was-on");
    case TaskbarState::Visible:
      break;
  }
  return QStringLiteral("visible");
}

QString formatTaskbarMarker(const TaskbarMarker& marker) {
  return QStringLiteral(
             "taskbar hidden by wabar\n"
             "pid=%1\n"
             "previous_autohide=%2\n"
             "windows_were_visible=%3\n")
      .arg(marker.pid)
      .arg(marker.changedAutoHide ? 0 : 1)
      .arg(marker.changedWindows ? 1 : 0);
}

std::optional<TaskbarMarker> parseTaskbarMarker(const QString& text) {
  const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  if (lines.isEmpty() || lines.first().trimmed() != QLatin1String("taskbar hidden by wabar")) {
    return std::nullopt;
  }
  TaskbarMarker marker;
  bool havePid = false;
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const qsizetype eq = lines.at(i).indexOf(QLatin1Char('='));
    if (eq < 0) continue;
    const QString key = lines.at(i).left(eq).trimmed();
    const QString value = lines.at(i).mid(eq + 1).trimmed();
    if (key == QLatin1String("pid")) {
      bool ok = false;
      marker.pid = value.toLongLong(&ok);
      havePid = ok;
    } else if (key == QLatin1String("previous_autohide")) {
      marker.changedAutoHide = value == QLatin1String("0");
    } else if (key == QLatin1String("windows_were_visible")) {
      marker.changedWindows = value == QLatin1String("1");
    }
  }
  if (!havePid) return std::nullopt;
  return marker;
}

namespace {

class FileMarkerStore : public TaskbarMarkerStore {
 public:
  explicit FileMarkerStore(QString path) : path_(std::move(path)) {}
  std::optional<TaskbarMarker> read() const override {
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return std::nullopt;
    return parseTaskbarMarker(QString::fromUtf8(file.read(4096)));
  }
  void write(const TaskbarMarker& marker) override {
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    file.write(formatTaskbarMarker(marker).toUtf8());
    file.commit();
  }
  void remove() override { QFile::remove(path_); }

 private:
  QString path_;
};

}  // namespace

std::unique_ptr<TaskbarMarkerStore> makeFileMarkerStore(const QString& path) {
  return std::make_unique<FileMarkerStore>(path);
}

WabarTaskbar::WabarTaskbar(TaskbarSystem& system, TaskbarMarkerStore& store, qint64 ownPid)
    : system_(system), store_(store), ownPid_(ownPid) {}

WabarTaskbar::~WabarTaskbar() { restore(); }

void WabarTaskbar::start(bool hideNow) {
  recoverStale();
  if (hideNow) hide();
}

bool WabarTaskbar::recoverStale() {
  if (hiding_) return false;
  const std::optional<TaskbarMarker> marker = store_.read();
  if (!marker) return false;
  if (marker->pid != ownPid_ && system_.processAlive(marker->pid)) return false;
  // Same order as restore(): the windows first, then auto-hide.
  if (marker->changedWindows) system_.setWindowsHidden(false);
  if (marker->changedAutoHide) system_.setAutoHide(false);
  store_.remove();
  return marker->changedWindows || marker->changedAutoHide;
}

void WabarTaskbar::hide() {
  if (hiding_) return;
  changedAutoHide_ = !system_.autoHide();
  changedWindows_ = system_.windowsVisible();
  // Only a run that changes something owns a marker: a second wabar that finds the taskbar
  // already hidden by the first must not overwrite the first one's marker.
  if (changedAutoHide_ || changedWindows_) {
    store_.write({ownPid_, changedAutoHide_, changedWindows_});
  }
  hiding_ = true;
  system_.onExplorerRestart([this] { reassert(); });
  // Auto-hide first: it releases the work area, which the bar's app bar then computes without.
  if (changedAutoHide_) system_.setAutoHide(true);
  if (changedWindows_) system_.setWindowsHidden(true);
}

void WabarTaskbar::restore() {
  if (!hiding_) return;
  hiding_ = false;
  system_.onExplorerRestart({});
  if (changedWindows_) system_.setWindowsHidden(false);
  if (changedAutoHide_) system_.setAutoHide(false);
  const bool hadMarker = changedWindows_ || changedAutoHide_;
  changedWindows_ = false;
  changedAutoHide_ = false;
  if (hadMarker) store_.remove();
}

TaskbarState WabarTaskbar::state() const {
  if (!system_.windowsVisible()) return TaskbarState::Hidden;
  return system_.autoHide() ? TaskbarState::AutoHide : TaskbarState::Visible;
}

void WabarTaskbar::reassert() {
  // Called when Explorer started again (its new taskbar windows are visible) and once a second
  // while hiding (Explorer shows an auto-hide taskbar again when the work area changes).
  // Auto-hide lives in the registry and survives, but is set again if Explorer dropped it.
  if (!hiding_) return;
  if (changedAutoHide_ && !system_.autoHide()) system_.setAutoHide(true);
  if (changedWindows_ && system_.windowsVisible()) system_.setWindowsHidden(true);
}

}  // namespace wabar::host
