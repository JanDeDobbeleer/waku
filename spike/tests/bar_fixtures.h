#pragma once

// Fixtures shared by the bar tests: a config home in a temp folder, a BarHost on fake native hooks
// and helpers to look inside a built window.

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
#include <vector>

#include "spike/bar/bar_host.h"

using namespace barnacle;

namespace {

// ---- config text ----

// One bar with three labelled widgets; `startText` lets a test change what the reload shows.
QString barConfig(const QString& startText = QStringLiteral("a"),
                  const QString& barProperties = QString()) {
  return QStringLiteral(R"(
import QtQuick
import barnacle

Config {
    Bar {
        %2
        layout: BarLayout {
            start: [ Text { objectName: "a"; text: "%1" } ]
            center: [ Text { objectName: "b"; text: "b" } ]
            end: [ Text { objectName: "c"; text: "c" } ]
        }
    }
}
)")
      .arg(startText, barProperties);
}

void write(const QString& path, const QString& text) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(text.toUtf8());
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
  return findItem(window->contentItem(), name);
}

QPointF positionIn(QQuickWindow* window, const QString& name) {
  QQuickItem* item = findItem(window, name);
  return item ? item->mapToScene(QPointF(0, 0)) : QPointF(-1, -1);
}

// ---- fake native layer ----

struct AppBarRecord {
  yasb::bar::AppBarRequest request;
};

struct Native {
  std::vector<AppBarRecord> created;
  int removed = 0;
  std::vector<Bar::Backdrop> backdrops;
  qlonglong monitor = 4242;

  BarHost::NativeHooks hooks() {
    class Recording final : public yasb::bar::IAppBar {
     public:
      Recording(Native* owner) : owner_(owner) {}
      void create(const yasb::bar::AppBarRequest& request) override {
        owner_->created.push_back({request});
      }
      void remove() override { ++owner_->removed; }

     private:
      Native* owner_;
    };
    BarHost::NativeHooks hooks;
    hooks.makeAppBar = [this]() -> std::unique_ptr<yasb::bar::IAppBar> {
      return std::make_unique<Recording>(this);
    };
    hooks.applyBackdrop = [this](quintptr, Bar::Backdrop backdrop) {
      backdrops.push_back(backdrop);
    };
    hooks.monitorHandle = [this](quintptr) { return monitor; };
    return hooks;
  }
};

// ---- the harness ----

struct Harness {
  QTemporaryDir dir;
  QQmlEngine engine;
  Native native;
  std::unique_ptr<BarHost> host;
  int screenCount = 1;
  bool allowReserve = false;

  QString barFile() const { return dir.filePath(QStringLiteral("bar.qml")); }

  void start(const QString& config) {
    ASSERT_TRUE(dir.isValid());
    write(barFile(), config);
    makeHost();
  }

  void makeHost() {
    BarHost::Options options;
    options.configHome = dir.path();
    options.allowReserve = allowReserve;
    options.hooks = native.hooks();
    const int count = screenCount;
    options.screens = [count] {
      // The offscreen platform has one screen; listing it twice gives two bars to place.
      QList<QScreen*> screens;
      for (int i = 0; i < count; ++i) screens << QGuiApplication::primaryScreen();
      return screens;
    };
    host = std::make_unique<BarHost>(&engine, std::move(options));
  }

  QQuickWindow* window(int i = 0) const { return host->windows().value(i); }
  QString text(const QString& name, int i = 0) {
    QQuickItem* item = findItem(window(i), name);
    return item ? item->property("text").toString() : QStringLiteral("<missing>");
  }
};

void settle() { QTest::qWait(60); }

}  // namespace
