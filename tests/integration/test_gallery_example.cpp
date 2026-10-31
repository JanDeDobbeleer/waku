// examples/gallery loads: a FloatingWindow with an image://thumb grid and a WindowThumbnail, the
// engine types meant for ported shells. Offscreen, so the thumbnail cache stays in memory and the
// WindowThumbnail makes no DWM call.
#include <gtest/gtest.h>

#include <QFile>

#include "host/qml/floating_window.h"
#include "host/testing/widget_fixtures.h"

namespace {

QString galleryExample() {
  QFile file(QStringLiteral(WABAR_EXAMPLES_DIR "/gallery/shell.qml"));
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QString::fromUtf8(file.readAll());
}

}  // namespace

TEST(GalleryExample, LoadsAFloatingWindowWithThumbnailsAndAWindowPreview) {
  const QString shell = galleryExample();
  ASSERT_FALSE(shell.isEmpty());
  Harness h;
  h.start(shell);
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->error().isEmpty()) << h.host->error().toStdString();
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_NE(qobject_cast<wabar::host::FloatingWindow*>(h.shellWindow()), nullptr);

  QQuickItem* thumb = item(h, QStringLiteral("thumb"));
  ASSERT_NE(thumb, nullptr);
  EXPECT_TRUE(
      thumb->property("source").toUrl().toString().startsWith(QStringLiteral("image://thumb/")))
      << thumb->property("source").toUrl().toString().toStdString();

  QQuickItem* preview = item(h, QStringLiteral("preview"));
  ASSERT_NE(preview, nullptr);
  EXPECT_EQ(preview->property("hwnd").toLongLong(), 0);
  EXPECT_FALSE(preview->property("available").toBool());
}
