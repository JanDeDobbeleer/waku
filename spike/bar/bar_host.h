#pragma once

#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QQmlComponent>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
#include <vector>

#include "core/bar/app_bar.h"
#include "spike/bar/bar_config.h"
#include "spike/bar/bar_theme.h"

class QQmlEngine;
class QQuickWindow;
class QScreen;

namespace barnacle {

// Builds the bars declared in <config home>\bar.qml and keeps them up to date.
//
// reload() first checks bar.qml and theme.qml in a throwaway engine (they compile, their types
// exist, their roots are a Config and a ThemeSpec). Only then are the Theme singleton filled and
// the current windows replaced by one window per bar and matching screen.
// A file that fails the check leaves the previous bars in place: `error` holds the first message
// (BarWindow shows it as a pill) and the full list goes to the log. If the very first load fails,
// or the check passes but building the windows still fails, there are no bars to keep, so a
// built-in fallback bar shows the error.
//
// Threading: GUI thread only. Owns the windows and the loaded config; they are destroyed in the
// right order (windows, then the config object, then its component) when replaced or when the host
// is destroyed. watch() reloads after a change in the config home, debounced.
class BarHost : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)

 public:
  // The parts that touch the real desktop, replaceable so tests never register an app bar or
  // blur a window.
  struct NativeHooks {
    // One per window; null function: no app bar object at all.
    std::function<std::unique_ptr<yasb::bar::IAppBar>()> makeAppBar;
    std::function<void(quintptr hwnd, Bar::Backdrop backdrop)> applyBackdrop;
    // HMONITOR of the monitor the window is on.
    std::function<qlonglong(quintptr hwnd)> monitorHandle;

    static NativeHooks win32();
  };

  struct Options {
    QString configHome;
    QString entryFile = QStringLiteral("bar.qml");
    // Optional: a ThemeSpec that sets the look; missing means the defaults.
    QString themeFile = QStringLiteral("theme.qml");
    // Reserving the work area changes the user's desktop, so it needs this on top of a bar's own
    // `reserveSpace` (the `--register-app-bar` switch).
    bool allowReserve = false;
    NativeHooks hooks;
    // Screens to build bars for; default: every screen of the application.
    std::function<QList<QScreen*>()> screens;
  };

  BarHost(QQmlEngine* engine, Options options, QObject* parent = nullptr);
  ~BarHost() override;

  // True when the config was applied. False leaves the previous bars in place.
  bool reload();
  // Reloads whenever a file in the config home changes.
  void watch();

  QString error() const { return error_; }
  QList<QQuickWindow*> windows() const;
  Config* config() const;

 signals:
  void errorChanged();
  void reloaded(bool ok);

 private:
  struct Entry {
    std::unique_ptr<QObject> object;
    QQuickWindow* window = nullptr;
    QScreen* screen = nullptr;
    Bar* spec = nullptr;
    std::unique_ptr<yasb::bar::IAppBar> appBar;
  };
  // Members are destroyed in reverse order: windows first, then the config, then its component.
  struct Loaded {
    // Takes the app bars off the desktop; the windows are destroyed right after.
    ~Loaded() {
      for (Entry& entry : entries) {
        if (entry.appBar) entry.appBar->remove();
      }
    }
    std::unique_ptr<QQmlComponent> component;
    std::unique_ptr<QObject> root;
    Config* config = nullptr;
    std::vector<Entry> entries;
  };

  // Loads `url` in a throwaway engine and checks it: it compiles and its root is an `expected`.
  bool validate(const QUrl& url, const QMetaObject& expected, QString* error);
  // theme.qml, when there is one, passes the same check (as a ThemeSpec).
  bool validateTheme(QString* error);
  // Copies theme.qml (or the defaults, when there is none) into the Theme singleton.
  void applyTheme();
  std::unique_ptr<Loaded> build(const QUrl& url, QString* error);
  void install(std::unique_ptr<Loaded> loaded);
  void present(Entry& entry);
  void setError(const QString& error);
  void refreshWatches();
  QString entryPath() const;
  QString themePath() const;

  QQmlEngine* engine_;
  Options options_;
  std::unique_ptr<QQmlComponent> windowComponent_;
  std::unique_ptr<Loaded> loaded_;
  QString error_;
  QFileSystemWatcher watcher_;
  QTimer debounce_;
  bool watching_ = false;
};

}  // namespace barnacle
