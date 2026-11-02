#pragma once

#include <QQuickImageProvider>
#include <QString>
#include <memory>

#include "features/tray/qml/tray_icon_model.h"

namespace waku::tray {

// image://tray/<id>/<revision>: the scaled icon of one TrayIconModel row. The revision only
// busts QML's image cache and is ignored here. Qt calls requestImage on the GUI thread for a
// synchronous Image and on its pixmap reader thread for `asynchronous: true`, so the provider never
// touches the model: it reads the model's mutex-guarded TrayImageStore, which it shares.
class TrayImageProvider : public QQuickImageProvider {
 public:
  explicit TrayImageProvider(TrayIconModel* model);

  QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

 private:
  std::shared_ptr<const TrayImageStore> store_;
};

}  // namespace waku::tray
