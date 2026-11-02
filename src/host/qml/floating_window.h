#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QSize>
#include <QString>

#include "host/qml/qs_window.h"

namespace waku::host {

// A normal decorated window (Quickshell `FloatingWindow`): a title bar, movable and resizable by
// the user. It opens centred on its screen at its implicit size.
class FloatingWindow : public QsWindow {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged FINAL)
  Q_PROPERTY(
      QSize minimumSize READ minimumSize WRITE setMinimumSize NOTIFY minimumSizeChanged FINAL)
  Q_PROPERTY(
      QSize maximumSize READ maximumSize WRITE setMaximumSize NOTIFY maximumSizeChanged FINAL)

 public:
  explicit FloatingWindow(QObject* parent = nullptr);

  QString title() const { return title_; }
  void setTitle(const QString& title);
  QSize minimumSize() const { return minimumSize_; }
  void setMinimumSize(const QSize& size);
  QSize maximumSize() const { return maximumSize_; }
  void setMaximumSize(const QSize& size);

 signals:
  void titleChanged();
  void minimumSizeChanged();
  void maximumSizeChanged();

 protected:
  void applyGeometry() override;
  Qt::WindowFlags windowFlags() const override;
  void afterShow() override { placed_ = true; }

 private:
  QString title_;
  QSize minimumSize_;
  QSize maximumSize_;
  bool placed_ = false;
};

}  // namespace waku::host
