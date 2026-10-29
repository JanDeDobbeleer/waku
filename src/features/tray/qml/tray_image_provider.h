#pragma once

#include <QPointer>
#include <QQuickImageProvider>
#include <QString>

#include "features/tray/qml/tray_icon_model.h"

namespace wabar::tray {

// image://tray/<id>/<revision>: the scaled icon of one TrayIconModel row. The revision only
// busts QML's image cache and is ignored here. Requests run synchronously on the GUI thread
// (Image providers without the threaded flag), where the model lives.
class TrayImageProvider : public QQuickImageProvider {
 public:
  explicit TrayImageProvider(TrayIconModel* model);

  QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

 private:
  QPointer<TrayIconModel> model_;
};

}  // namespace wabar::tray
