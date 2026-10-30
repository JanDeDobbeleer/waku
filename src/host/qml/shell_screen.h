#pragma once

#include <QtQml/qqmlregistration.h>

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlListProperty>
#include <QScreen>
#include <QStringList>
#include <QVariantList>

class QQmlEngine;

namespace wabar::host {

// One screen as QML sees it (Quickshell `ShellScreen`): a window's `screen` and an entry of
// `ScreenList.screens`. Geometry is logical pixels in the virtual desktop. Owned by a ScreenList.
class ShellScreen : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("ShellScreen comes from ScreenList.screens")
  // "\\.\DISPLAY1"
  Q_PROPERTY(QString name READ name CONSTANT FINAL)
  Q_PROPERTY(int x READ x NOTIFY geometryChanged FINAL)
  Q_PROPERTY(int y READ y NOTIFY geometryChanged FINAL)
  Q_PROPERTY(int width READ width NOTIFY geometryChanged FINAL)
  Q_PROPERTY(int height READ height NOTIFY geometryChanged FINAL)
  Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio NOTIFY geometryChanged FINAL)
  Q_PROPERTY(QString model READ model CONSTANT FINAL)
  Q_PROPERTY(QString serialNumber READ serialNumber CONSTANT FINAL)
  // wabar: the primary screen of the desktop.
  Q_PROPERTY(bool primary READ primary CONSTANT FINAL)

 public:
  ShellScreen(QScreen* screen, bool primary, QObject* parent = nullptr);

  QScreen* qscreen() const { return screen_; }
  QString name() const { return name_; }
  int x() const { return geometry().x(); }
  int y() const { return geometry().y(); }
  int width() const { return geometry().width(); }
  int height() const { return geometry().height(); }
  qreal devicePixelRatio() const { return screen_ ? screen_->devicePixelRatio() : 1.0; }
  QString model() const { return screen_ ? screen_->model() : QString(); }
  QString serialNumber() const { return screen_ ? screen_->serialNumber() : QString(); }
  bool primary() const { return primary_; }
  QRect geometry() const { return screen_ ? screen_->geometry() : QRect(); }

 signals:
  void geometryChanged();

 private:
  QPointer<QScreen> screen_;
  QString name_;
  bool primary_ = false;
};

// The screens shells are built for, one list per engine. ShellHost fills it before it builds a
// shell (tests give it any screens); without a host it holds the application's screens.
//
// Quickshell spells this `Quickshell.screens`. The `Quickshell` singleton is not defined here:
// src/host/quickshell/qml/Quickshell.qml forwards its `screens` to this list.
//
//     Variants {
//         model: ScreenList.screens
//         PanelWindow { required property ShellScreen modelData; screen: modelData }
//     }
class ScreenList : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(QQmlListProperty<wabar::host::ShellScreen> screens READ screensProperty NOTIFY
                 screensChanged FINAL)
  Q_PROPERTY(wabar::host::ShellScreen* primary READ primary NOTIFY screensChanged FINAL)

 public:
  explicit ScreenList(QObject* parent = nullptr);

  // The engine's list (the singleton instance); null when `engine` has none or no wabar import.
  static ScreenList* of(QQmlEngine* engine);

  // Replaces the list. `primary` is the screen "primary" stands for; null: the first of `screens`.
  void setScreens(const QList<QScreen*>& screens, QScreen* primary);

  const QList<ShellScreen*>& list() const { return screens_; }
  ShellScreen* primary() const;
  // The ShellScreen of `screen`, or null.
  ShellScreen* find(QScreen* screen) const;

  // The screens selected by `patterns` ("primary", "all", screen names; host/screen_match.h), in
  // screen order. The `screens` shorthand of Panel.
  Q_INVOKABLE QVariantList match(const QStringList& patterns) const;

 signals:
  void screensChanged();

 private:
  QQmlListProperty<ShellScreen> screensProperty();

  QList<ShellScreen*> screens_;
};

}  // namespace wabar::host
