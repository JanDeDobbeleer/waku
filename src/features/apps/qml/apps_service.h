#pragma once

#include <QtQml/qqmlregistration.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "features/apps/apps_platform.h"
#include "features/apps/catalog.h"
#include "features/apps/frecency.h"
#include "features/apps/qml/app_icon_provider.h"
#include "features/apps/qml/apps_model.h"
#include "features/apps/qml/desktop_entry.h"

class QFileSystemWatcher;
class QJSEngine;
class QQmlEngine;

namespace waku::apps {

// The last complete app catalogue of the process and the watcher on the Start Menu folders, shared
// by the AppsService of every engine and kept through a reload (acquireAppCatalog, a
// kernel::LingeringHub). A reload deletes the singleton and builds a new one at once: without the
// cache the new one scanned again (every .lnk through IShellLink, all of shell:AppsFolder; 0.5 to
// 3 s of COM on the pool) and registered the watcher on every folder again.
//
// Threads: GUI thread only. The watcher is Qt's (its own thread); `foldersChanged` arrives here.
class AppCatalog : public QObject {
  Q_OBJECT

 public:
  explicit AppCatalog(QObject* parent = nullptr);

  // The catalogue of the last complete scan and how long it took, unless a watched folder changed
  // since.
  bool fresh() const { return fresh_; }
  const std::vector<AppRecord>& catalog() const { return catalog_; }
  int scanMs() const { return scanMs_; }
  void store(std::vector<AppRecord> catalog, int scanMs);

  // Watches exactly `directories`: adds the new ones and removes the gone ones, not all of them
  // again after every scan.
  void watch(const QStringList& directories);
  QStringList watched() const;

 signals:
  // A watched folder changed; the catalogue is no longer fresh.
  void foldersChanged();

 private:
  QFileSystemWatcher* watcher_ = nullptr;
  std::vector<AppRecord> catalog_;
  int scanMs_ = 0;
  bool fresh_ = false;
};

// The process's catalogue, created on first use; it outlives its last holder by
// kernel::kReloadLingerMs.
std::shared_ptr<AppCatalog> acquireAppCatalog();

// The app catalogue of a `waku` run, as a QML singleton: Start Menu shortcuts (.lnk files below
// FOLDERID_Programs and FOLDERID_CommonPrograms) and the Store apps of shell:AppsFolder, as
// Quickshell-shaped DesktopEntry objects. `import Quickshell` shows it as `DesktopEntries`
// (src/quickshell/core/qml/DesktopEntries.qml forwards here), waku shells use it directly as
// `AppsService`. Registers image://appicon/<id> (see AppIconProvider).
//
// `applications` is a model of DesktopEntry with a `values` array, sorted by name. `ready` turns
// true after the first complete scan; shortcuts show up first (`loading` stays true until the Store
// apps follow). `scanMs` is how long the last scan took on its worker.
//
// Threads: GUI thread for everything QML sees. A scan runs on the global QThreadPool (shortcuts,
// then shell:AppsFolder, each in COM's MTA of that thread) and posts its result to the GUI thread;
// the destructor detaches a running scan (it only posts to a dead owner, which is dropped). The
// icon provider has its own pool. A QFileSystemWatcher on the Start Menu folders restarts the
// scan after `debounceMs` of quiet; with a shared AppCatalog (the real service) the watcher and
// the last complete catalogue live there, so the service of a reload starts from that catalogue
// without a scan. A rescan posts only its complete result; the shortcuts alone come first only
// while the list is empty.
class AppsService : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(waku::apps::AppsModel* applications READ applications CONSTANT FINAL)
  Q_PROPERTY(bool ready READ ready NOTIFY readyChanged FINAL)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
  Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
  Q_PROPERTY(int scanMs READ scanMs NOTIFY scanFinished FINAL)

 public:
  // Everything a test replaces. The real constructor fills it when kernel::liveDesktop("apps");
  // otherwise it stays empty: no folders, no Store apps, nothing launches, no icons.
  struct Deps {
    QStringList folders;
    ShortcutResolver resolver;
    AppsFolderEnumerator appsFolder;
    AppLauncher launcher;
    IconLoader iconLoader;
    QString frecencyPath;  // "": not persisted
    bool watch = false;
    int debounceMs = 1500;
    bool async = true;  // false: refresh() scans on the calling thread (tests)
    // The shared catalogue and watcher (the real service); null: this service watches for itself
    // when `watch`, and every service scans.
    std::shared_ptr<AppCatalog> catalog;
  };

  // Deliberately not default-constructible: create() must run to register the image provider.
  explicit AppsService(QObject* parent);
  explicit AppsService(Deps deps, QObject* parent = nullptr);
  ~AppsService() override;

  static AppsService* create(QQmlEngine* engine, QJSEngine*);
  // Tests: create() builds the singleton with this factory instead of the real one.
  static void setFactoryForTests(std::function<std::unique_ptr<AppsService>()> factory);

  AppsModel* applications() { return &model_; }
  bool ready() const { return ready_; }
  bool loading() const { return loading_; }
  int count() const { return model_.count(); }
  int scanMs() const { return scanMs_; }
  const std::shared_ptr<IconIndex>& iconIndex() const { return iconIndex_; }
  const IconLoader& iconLoader() const { return deps_.iconLoader; }

  // Quickshell's DesktopEntries.byId: the entry with this id (case-insensitive), null when none.
  Q_INVOKABLE waku::apps::DesktopEntry* byId(const QString& id) const;
  // Quickshell's DesktopEntries.heuristicLookup: the entry a window class or process name most
  // likely belongs to (id, name, executable name, last id segment, AUMID), null when none.
  Q_INVOKABLE waku::apps::DesktopEntry* heuristicLookup(const QString& name) const;
  // The entries matching `query`, best first (see rank.h), at most `limit` (all when <= 0). A
  // blank query lists the most used apps first.
  Q_INVOKABLE QVariantList search(const QString& query, int limit = 0) const;
  // Start the app, or raise it. True when the shell accepted the request; records the launch for
  // the ranking.
  Q_INVOKABLE bool launch(const QString& id);
  // Start the app again even when it already runs (a new window, not elevated); never raises the
  // running one. True when the shell accepted the request; records the launch for the ranking.
  Q_INVOKABLE bool launchNew(const QString& id);
  // Start the app elevated (UAC prompt): a shortcut, or a Store app with an execution alias. Always
  // a new process. False otherwise and for unknown ids.
  Q_INVOKABLE bool launchAsAdmin(const QString& id);
  // Show the shortcut's target in Explorer. False for Store apps and unknown ids.
  Q_INVOKABLE bool openLocation(const QString& id);
  // Rescans the Start Menu and shell:AppsFolder.
  Q_INVOKABLE void refresh();

  // Whether `id` is a known app, and its record (tests).
  const AppRecord* recordFor(const QString& id) const;

 signals:
  void readyChanged();
  void loadingChanged();
  void countChanged();
  // A scan finished and the model holds its result.
  void scanFinished();

 private:
  struct ScanState {
    std::mutex mutex;
    AppsService* owner = nullptr;  // null once the service is gone
  };
  struct ScanResult {
    std::vector<AppRecord> catalog;
    bool complete = false;
    int ms = 0;
  };

  // Scans on the calling thread and hands the result to `sink`: the shortcuts alone first when
  // there is a shell:AppsFolder enumerator, then the complete catalogue.
  static void runScan(const Deps& deps, bool interim, const std::function<void(ScanResult)>& sink);
  void applyResult(ScanResult result);
  void startScan();
  bool launchMode(const QString& id, LaunchMode mode);
  DesktopEntry* find(const QString& id) const;
  void watchFolders();

  Deps deps_;
  AppsModel model_;
  FrecencyStore frecency_;
  std::shared_ptr<IconIndex> iconIndex_ = std::make_shared<IconIndex>();
  std::shared_ptr<ScanState> state_ = std::make_shared<ScanState>();
  QHash<QString, DesktopEntry*> entries_;  // by lower-case id
  QFileSystemWatcher* watcher_ = nullptr;
  QTimer debounce_;
  bool ready_ = false;
  bool loading_ = false;
  bool scanning_ = false;
  bool rescan_ = false;
  int scanMs_ = 0;
};

}  // namespace waku::apps
