#pragma once

// Fixtures shared by the shell tests: a config home in a temp folder, a ShellHost on fake native
// hooks and helpers to look inside a built window.

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "control/control.h"
#include "control/wallpaper_control.h"
#include "host/qml/qs_window.h"
#include "host/qml/shell_host.h"

using namespace wabar::control;
using namespace wabar::host;

namespace {

// ---- shell text ----

// One panel with three labelled widgets; `startText` lets a test change what the reload shows.
QString shellConfig(const QString& startText = QStringLiteral("a"),
                    const QString& panelProperties = QString()) {
  return QStringLiteral(R"(
import QtQuick
import wabar

ShellRoot {
    Panel {
        %2
        content: PanelLayout {
            start: [ Text { objectName: "a"; text: "%1" } ]
            center: [ Text { objectName: "b"; text: "b" } ]
            end: [ Text { objectName: "c"; text: "c" } ]
        }
    }
}
)")
      .arg(startText, panelProperties);
}

void write(const QString& path, const QString& text) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(text.toUtf8());
}

// A top-level window by objectName (popups and tooltips are windows of their own).
QQuickWindow* findWindow(const QString& name) {
  for (QWindow* window : QGuiApplication::allWindows()) {
    if (window->objectName() == name) return qobject_cast<QQuickWindow*>(window);
  }
  return nullptr;
}

// ---- finding things in a window ----

QQuickItem* findItem(QQuickItem* root, const QString& name) {
  if (root->objectName() == name) return root;
  for (QQuickItem* child : root->childItems()) {
    if (QQuickItem* found = findItem(child, name)) return found;
  }
  return nullptr;
}

QQuickItem* findItem(QQuickWindow* window, const QString& name) {
  return window ? findItem(window->contentItem(), name) : nullptr;
}

QPointF positionIn(QQuickWindow* window, const QString& name) {
  QQuickItem* item = findItem(window, name);
  return item ? item->mapToScene(QPointF(0, 0)) : QPointF(-1, -1);
}

// ---- fake native layer ----

struct AppBarRecord {
  wabar::host::AppBarRequest request;
};

struct Native {
  std::vector<AppBarRecord> created;
  int removed = 0;
  // What the fake system pushes every app bar inward by, physical pixels; and the requests to
  // renegotiate (a later thickness or screen change) after the first.
  int appBarInset = 0;
  std::vector<AppBarRecord> repositioned;
  std::vector<Backdrop::Kind> backdrops;
  qlonglong monitor = 4242;
  // The fake cursor (physical pixels) that window masks read; never the real one.
  std::optional<QPoint> cursor = QPoint(-100000, -100000);
  // False: no cursor source at all, like a host that is not on a live desktop.
  bool cursorHook = true;
  // What the window hooks were asked: raised to topmost, WS_EX_NOACTIVATE set (true) or cleared,
  // given the foreground. Nothing here touches a real window.
  std::vector<quintptr> raised;
  std::vector<std::pair<quintptr, bool>> noActivate;
  std::vector<quintptr> foregrounded;

  ShellHost::NativeHooks hooks() {
    class Recording final : public wabar::host::IAppBar {
     public:
      Recording(Native* owner) : owner_(owner) {}
      int create(const wabar::host::AppBarRequest& request) override {
        owner_->created.push_back({request});
        return owner_->appBarInset;
      }
      int reposition(const wabar::host::AppBarRequest& request) override {
        owner_->repositioned.push_back({request});
        return owner_->appBarInset;
      }
      void remove() override { ++owner_->removed; }

     private:
      Native* owner_;
    };
    ShellHost::NativeHooks hooks;
    hooks.makeAppBar = [this]() -> std::unique_ptr<wabar::host::IAppBar> {
      return std::make_unique<Recording>(this);
    };
    hooks.applyBackdrop = [this](quintptr, Backdrop::Kind backdrop) {
      backdrops.push_back(backdrop);
    };
    hooks.monitorHandle = [this](quintptr) { return monitor; };
    if (cursorHook) hooks.cursorPosition = [this]() { return cursor; };
    hooks.raiseTopmost = [this](quintptr hwnd) { raised.push_back(hwnd); };
    hooks.setNoActivate = [this](quintptr hwnd, bool value) {
      noActivate.emplace_back(hwnd, value);
    };
    hooks.forceForeground = [this](quintptr hwnd) { foregrounded.push_back(hwnd); };
    return hooks;
  }
};

// ---- the harness ----

struct Harness {
  QTemporaryDir dir;
  QQmlEngine engine;
  Native native;
  std::unique_ptr<ShellHost> host;
  int screenCount = 1;
  bool allowReserve = false;
  bool safe = false;
  QUrl safeUrl;
  int screenSettleMs = ShellHost::Options().screenSettleMs;

  QString shellFile() const { return dir.filePath(QStringLiteral("shell.qml")); }

  void start(const QString& config) {
    ASSERT_TRUE(dir.isValid());
    write(shellFile(), config);
    makeHost();
  }

  void makeHost() {
    ShellHost::Options options;
    options.configHome = dir.path();
    options.allowReserve = allowReserve;
    options.safe = safe;
    options.safeUrl = safeUrl;
    options.screenSettleMs = screenSettleMs;
    options.hooks = native.hooks();
    const int count = screenCount;
    options.screens = [count] {
      // The offscreen platform has one screen; listing it twice gives two windows to place.
      QList<QScreen*> screens;
      for (int i = 0; i < count; ++i) screens << QGuiApplication::primaryScreen();
      return screens;
    };
    host = std::make_unique<ShellHost>(&engine, std::move(options));
  }

  QQuickWindow* window(int i = 0) const { return host->windows().value(i); }
  // The declaration behind window(i): a PanelWindow or FloatingWindow.
  QsWindow* shellWindow(int i = 0) const { return QsWindow::of(window(i)); }
  QString text(const QString& name, int i = 0) {
    QQuickItem* item = findItem(window(i), name);
    return item ? item->property("text").toString() : QStringLiteral("<missing>");
  }
};

void settle() { QTest::qWait(60); }

}  // namespace
