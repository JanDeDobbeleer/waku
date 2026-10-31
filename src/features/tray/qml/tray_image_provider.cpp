#include "features/tray/qml/tray_image_provider.h"

namespace wabar::tray {

TrayImageProvider::TrayImageProvider(TrayIconModel* model)
    : QQuickImageProvider(QQuickImageProvider::Image),
      store_(model ? model->imageStore() : nullptr) {}

QImage TrayImageProvider::requestImage(const QString& id, QSize* size,
                                       const QSize& /*requestedSize*/) {
  if (size) *size = QSize(0, 0);
  if (!store_) return {};
  bool ok = false;
  const int rowId = id.section(QLatin1Char('/'), 0, 0).toInt(&ok);
  if (!ok) return {};
  const QImage image = store_->image(rowId);
  if (image.isNull()) return {};
  if (size) *size = image.size();
  return image;
}

}  // namespace wabar::tray
