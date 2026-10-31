#include "host/qml/theme.h"

#include <QFontDatabase>
#include <QMetaProperty>

namespace wabar::host {

ThemeValues::ThemeValues(QObject* parent)
    : QObject(parent),
      fontFamily_(QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()) {}

void ThemeValues::assignFrom(const ThemeValues& other) {
  const QMetaObject& meta = ThemeValues::staticMetaObject;
  for (int i = meta.propertyOffset(); i < meta.propertyCount(); ++i) {
    const QMetaProperty property = meta.property(i);
    if (!property.isWritable()) continue;  // the derived fills follow their colours
    property.write(this, property.read(&other));
  }
  // The bar fills stay "follow the fill" when the other one does.
  barFill_ = other.barFill_;
  barFillHover_ = other.barFillHover_;
}

}  // namespace wabar::host
