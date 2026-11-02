#pragma once

// Helpers for tests that put built-in widgets (waku.widgets) into a real panel on the offscreen
// platform. On top of shell_fixtures.h (the Harness).

#include "host/testing/shell_fixtures.h"

namespace {

QString widgetConfig(const QString& widgets) {
  return QStringLiteral(R"(
import QtQuick
import waku
import waku.widgets
ShellRoot {
    Panel {
        content: PanelLayout {
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
