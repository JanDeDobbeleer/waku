#include "spike/services/tray_image_provider.h"

namespace wabar::services {

TrayImageProvider::TrayImageProvider(TrayIconModel* model)
    : QQuickImageProvider(QQuickImageProvider::Image), model_(model) {}

QImage TrayImageProvider::requestImage(const QString& id, QSize* size,
                                       const QSize& /*requestedSize*/) {
  if (size) *size = QSize(0, 0);
  if (!model_) return {};
  bool ok = false;
  const int rowId = id.section(QLatin1Char('/'), 0, 0).toInt(&ok);
  if (!ok) return {};
  const QImage image = model_->image(rowId);
  if (image.isNull()) return {};
  if (size) *size = image.size();
  return image;
}

}  // namespace wabar::services
