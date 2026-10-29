#pragma once

// Helpers for tests that put built-in widgets (wabar.widgets) into a real bar on the offscreen
// platform. On top of bar_fixtures.h (the Harness).

#include "host/testing/bar_fixtures.h"

namespace {

QString widgetConfig(const QString& widgets) {
  return QStringLiteral(R"(
import QtQuick
import wabar
import wabar.widgets
Config {
    Bar {
        layout: BarLayout {
            start: [ %1 ]
        }
    }
}
)")
      .arg(widgets);
}

QQuickItem* item(Harness& h, const QString& name) { return findItem(h.window(), name); }

QString text(Harness& h, const QString& name) {
  QQuickItem* i = item(h, name);
  return i ? i->property("text").toString() : QStringLiteral("<missing>");
}

bool started(Harness& h) {
  if (!h.host->reload()) return false;
  QQuickWindow* window = h.window();
  return window && QTest::qWaitForWindowExposed(window);
}

QPoint centerOf(Harness& h, const QString& name) {
  QQuickItem* i = item(h, name);
  return i->mapToScene(QPointF(i->width() / 2, i->height() / 2)).toPoint();
}

}  // namespace
