#pragma once

#include <QtQml/qqmlregistration.h>

#include <QColor>
#include <QList>
#include <QObject>
#include <QQmlComponent>
#include <QQmlListProperty>
#include <QStringList>

namespace barnacle {

// One bar, as declared in bar.qml. A declaration, not a window: the host builds one window per
// matching screen and instantiates `layout` inside it. All properties are read when the config is
// loaded; a change needs a reload (the host reloads when a config file changes).
class Bar : public QObject {
  Q_OBJECT
  QML_ELEMENT
  // "primary", "all", or a screen name ("\\.\DISPLAY2" or just "DISPLAY2").
  Q_PROPERTY(QStringList screens MEMBER screens_ NOTIFY changed FINAL)
  Q_PROPERTY(Position position MEMBER position_ NOTIFY changed FINAL)
  Q_PROPERTY(int height MEMBER height_ NOTIFY changed FINAL)
  // Register as an app bar and reserve the work area (only when the host allows it).
  Q_PROPERTY(bool reserveSpace MEMBER reserveSpace_ NOTIFY changed FINAL)
  Q_PROPERTY(Backdrop backdrop MEMBER backdrop_ NOTIFY changed FINAL)
  // Unset: the bar uses Theme.surface.
  Q_PROPERTY(QColor background MEMBER background_ NOTIFY changed FINAL)
  Q_PROPERTY(bool hasBackground READ hasBackground NOTIFY changed FINAL)
  Q_PROPERTY(bool enabled MEMBER enabled_ NOTIFY changed FINAL)
  // What fills the bar; built once per screen. Usually a BarLayout.
  Q_PROPERTY(QQmlComponent* layout MEMBER layout_ NOTIFY changed FINAL)

 public:
  enum Position { Top, Bottom };
  Q_ENUM(Position)
  enum Backdrop { None, Blur, Mica };
  Q_ENUM(Backdrop)

  explicit Bar(QObject* parent = nullptr) : QObject(parent) {}

  QStringList screens() const { return screens_; }
  Position position() const { return position_; }
  int height() const { return height_; }
  bool reserveSpace() const { return reserveSpace_; }
  Backdrop backdrop() const { return backdrop_; }
  QColor background() const { return background_; }
  bool hasBackground() const { return background_.isValid(); }
  bool enabled() const { return enabled_; }
  QQmlComponent* layout() const { return layout_; }

 signals:
  void changed();

 private:
  QStringList screens_{QStringLiteral("primary")};
  Position position_ = Top;
  int height_ = 40;
  bool reserveSpace_ = false;
  Backdrop backdrop_ = None;
  QColor background_;
  bool enabled_ = true;
  QQmlComponent* layout_ = nullptr;
};

// The root of bar.qml: the bars it declares, in order.
class Config : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_CLASSINFO("DefaultProperty", "bars")
  Q_PROPERTY(QQmlListProperty<barnacle::Bar> bars READ bars FINAL)

 public:
  explicit Config(QObject* parent = nullptr) : QObject(parent) {}

  QQmlListProperty<Bar> bars() { return QQmlListProperty<Bar>(this, &bars_); }
  const QList<Bar*>& barList() const { return bars_; }

 private:
  QList<Bar*> bars_;
};

}  // namespace barnacle
