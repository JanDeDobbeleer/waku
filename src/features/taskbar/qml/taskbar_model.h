#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWindow>
#include <vector>

#include "features/taskbar/qml/taskbar_service.h"

namespace waku::taskbar {

// The buttons of one Taskbar widget: the service's windows filtered for this widget (monitor,
// strict, hidden, ignore lists) and grouped per app, after the pinned apps. Recomputed from the
// service on every change and applied as removals, insertions, moves and dataChanged, so open
// menus and delegates survive. GUI thread only; no thread of its own.
//
// Order: pinned apps in pin-file order (a pinned app whose windows are all filtered out, or that
// does not run, is a button of its own that launches it), then the other buttons by the arrival
// of their first window.
class TaskbarModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(waku::taskbar::TaskbarService* service READ service WRITE setService NOTIFY
                 serviceChanged FINAL)
  Q_PROPERTY(QWindow* window READ window WRITE setWindow NOTIFY windowChanged FINAL)
  Q_PROPERTY(bool grouping READ grouping WRITE setGrouping NOTIFY filterChanged FINAL)
  Q_PROPERTY(
      bool showOnlyVisible READ showOnlyVisible WRITE setShowOnlyVisible NOTIFY filterChanged FINAL)
  Q_PROPERTY(
      bool strictFiltering READ strictFiltering WRITE setStrictFiltering NOTIFY filterChanged FINAL)
  Q_PROPERTY(bool monitorExclusive READ monitorExclusive WRITE setMonitorExclusive NOTIFY
                 filterChanged FINAL)
  Q_PROPERTY(
      QStringList ignoreTitles READ ignoreTitles WRITE setIgnoreTitles NOTIFY filterChanged FINAL)
  Q_PROPERTY(QStringList ignoreClasses READ ignoreClasses WRITE setIgnoreClasses NOTIFY
                 filterChanged FINAL)
  Q_PROPERTY(QStringList ignoreProcesses READ ignoreProcesses WRITE setIgnoreProcesses NOTIFY
                 filterChanged FINAL)
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged FINAL)

 public:
  enum Role {
    KeyRole = Qt::UserRole + 1,
    TitleRole,
    TooltipRole,
    IconUrlRole,  // image://windowicon/<key>/<revision>
    CountRole,    // windows behind the button
    PinnedRole,
    RunningRole,
    ActiveRole,
    MinimizedRole,
    FlashingRole,
    UniqueIdRole,
    ExeRole,            // process name of the (first) window, "foo.exe"
    ClassNameRole,      // window class of it
    HwndRole,           // its handle, as a number
    MonitorHandleRole,  // the HMONITOR it is on, 0 when unknown (matches Workspaces monitors)
    ProgressStateRole,  // "none", "indeterminate", "normal", "error", "paused"
    ProgressRole,       // 0..1
  };

  explicit TaskbarModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  TaskbarService* service() const { return service_; }
  void setService(TaskbarService* service);
  QWindow* window() const { return window_; }
  // The bar window: starts the service's tracker on it and, with monitorExclusive, says which
  // monitor this widget is on (following the window when it moves).
  void setWindow(QWindow* window);
  bool grouping() const { return grouping_; }
  void setGrouping(bool value);
  bool showOnlyVisible() const { return showOnlyVisible_; }
  void setShowOnlyVisible(bool value);
  bool strictFiltering() const { return strictFiltering_; }
  void setStrictFiltering(bool value);
  bool monitorExclusive() const { return monitorExclusive_; }
  void setMonitorExclusive(bool value);
  QStringList ignoreTitles() const { return ignoreTitles_; }
  void setIgnoreTitles(const QStringList& value);
  QStringList ignoreClasses() const { return ignoreClasses_; }
  void setIgnoreClasses(const QStringList& value);
  QStringList ignoreProcesses() const { return ignoreProcesses_; }
  void setIgnoreProcesses(const QStringList& value);

  // Left click: focus, minimise or restore the window; cycle through a group; launch a pinned
  // app that does not run.
  Q_INVOKABLE void activate(int row);
  // Middle click: close the window (the first of a group); launch a pinned app that does not run.
  Q_INVOKABLE void middleClick(int row);
  Q_INVOKABLE void closeWindow(int row);
  Q_INVOKABLE void closeAll(int row);
  // Pins the app of the button, or unpins it.
  Q_INVOKABLE void togglePin(int row);
  // Starts the pinned app of the button.
  Q_INVOKABLE void launch(int row);

 signals:
  void serviceChanged();
  void windowChanged();
  void filterChanged();
  void countChanged();

 private:
  struct Row {
    QString key;
    QString uniqueId;
    std::vector<WindowHandle> members;  // empty for a pinned app that does not run
    WindowHandle iconKey = 0;
    QString title;
    QString tooltip;
    QString iconUrl;
    int count = 0;
    bool pinned = false;
    bool running = false;
    bool active = false;
    bool minimized = false;
    bool flashing = false;
    QString exe;
    QString className;
    WindowHandle hwnd = 0;
    WindowHandle monitor = 0;
    WindowProgress progress;

    bool operator==(const Row&) const = default;
  };

  void rebuild();
  std::vector<Row> compute();
  bool visibleHere(const TaskbarWindow& w) const;
  bool shownNow(WindowHandle hwnd) const;
  void apply(std::vector<Row> rows);
  void updateMonitor();
  const Row* rowAt(int row) const;

  QPointer<TaskbarService> service_;
  QPointer<QWindow> window_;
  bool grouping_ = false;
  bool showOnlyVisible_ = false;
  bool strictFiltering_ = true;
  bool monitorExclusive_ = false;
  QStringList ignoreTitles_;
  QStringList ignoreClasses_;
  QStringList ignoreProcesses_;
  WindowHandle monitor_ = 0;
  std::vector<Row> rows_;
  bool rebuilding_ = false;
};

}  // namespace waku::taskbar
