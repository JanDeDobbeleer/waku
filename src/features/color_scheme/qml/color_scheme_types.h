#pragma once

#include <QtQml/qqmlregistration.h>

#include <QColor>
#include <QString>
#include <QVariantMap>

namespace wabar::color_scheme {

// The value `ColorScheme.fromColor()` and friends return, and `fromImageFinished` carries. QML
// type `scheme`. `colors` maps every Material 3 role to a colour twice: by its plain name
// (`primary`, `onPrimary`, `surfaceContainerHigh`, `primary_paletteKeyColor`) and under the
// caelestia alias with an `m3` prefix (`m3primary`, `m3onPrimary`, ...).
struct SchemeValue {
  Q_GADGET
  QML_VALUE_TYPE(scheme)
  Q_PROPERTY(bool valid MEMBER valid)
  Q_PROPERTY(QString variant MEMBER variant)
  Q_PROPERTY(bool dark MEMBER dark)
  Q_PROPERTY(double contrast MEMBER contrast)
  Q_PROPERTY(QColor source MEMBER source)
  Q_PROPERTY(QVariantMap colors MEMBER colors)

 public:
  bool valid = false;
  QString variant;
  bool dark = false;
  double contrast = 0.0;
  QColor source;
  QVariantMap colors;
};

}  // namespace wabar::color_scheme
