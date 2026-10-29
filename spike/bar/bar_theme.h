#pragma once

#include <QtQml/qqmlregistration.h>

#include <QColor>
#include <QObject>
#include <QString>

namespace tidy {

// The look of every bar and widget: colours, the font and a few sizes. Widgets read it as
// `Theme.accent`, `Theme.fontSize`, ... and follow a change at once.
//
// This base class holds the properties and their defaults; Theme (the singleton widgets read) and
// ThemeSpec (the root of theme.qml) both derive from it, so a theme file can set any property by
// the same name and leave the rest at the default.
class ThemeValues : public QObject {
  Q_OBJECT
  QML_ANONYMOUS

  // Colours. Translucent by default: the bars blur what is behind them.
  Q_PROPERTY(QColor text MEMBER text_ NOTIFY changed FINAL)
  Q_PROPERTY(QColor surface MEMBER surface_ NOTIFY changed FINAL)
  Q_PROPERTY(QColor accent MEMBER accent_ NOTIFY changed FINAL)
  Q_PROPERTY(QColor warn MEMBER warn_ NOTIFY changed FINAL)
  Q_PROPERTY(QColor good MEMBER good_ NOTIFY changed FINAL)
  Q_PROPERTY(QColor caution MEMBER caution_ NOTIFY changed FINAL)
  // The background of a widget: normal, hovered, active (shown) and in use (has content).
  Q_PROPERTY(QColor fill MEMBER fill_ NOTIFY changed FINAL)
  Q_PROPERTY(QColor fillHover MEMBER fillHover_ NOTIFY changed FINAL)
  Q_PROPERTY(QColor fillActive MEMBER fillActive_ NOTIFY changed FINAL)
  Q_PROPERTY(QColor fillUsed MEMBER fillUsed_ NOTIFY changed FINAL)
  Q_PROPERTY(QColor fillEmpty MEMBER fillEmpty_ NOTIFY changed FINAL)
  // The background of a widget that sits on a bar (not in a popup): normal and hovered. They
  // follow `fill` and `fillHover` until set; a flat look sets `barFill` transparent. Popup rows
  // keep `fill`, so they stay visible.
  Q_PROPERTY(QColor barFill READ barFill WRITE setBarFill NOTIFY changed FINAL)
  Q_PROPERTY(QColor barFillHover READ barFillHover WRITE setBarFillHover NOTIFY changed FINAL)

  // Popups and tooltips: nearly opaque, because they sit over arbitrary windows.
  Q_PROPERTY(QColor popupSurface MEMBER popupSurface_ NOTIFY changed FINAL)
  Q_PROPERTY(QColor popupBorder MEMBER popupBorder_ NOTIFY changed FINAL)

  // Derived from the colours above, read only: a translucent version for a widget background.
  Q_PROPERTY(QColor accentFill READ accentFill NOTIFY changed FINAL)
  Q_PROPERTY(QColor warnFill READ warnFill NOTIFY changed FINAL)
  Q_PROPERTY(QColor goodFill READ goodFill NOTIFY changed FINAL)
  Q_PROPERTY(QColor cautionFill READ cautionFill NOTIFY changed FINAL)

  // Type and size, in pixels. `radius` < 0 means fully rounded (a pill).
  Q_PROPERTY(QString fontFamily MEMBER fontFamily_ NOTIFY changed FINAL)
  Q_PROPERTY(int fontSize MEMBER fontSize_ NOTIFY changed FINAL)
  Q_PROPERTY(int smallFontSize MEMBER smallFontSize_ NOTIFY changed FINAL)
  Q_PROPERTY(int pillHeight MEMBER pillHeight_ NOTIFY changed FINAL)
  Q_PROPERTY(int radius MEMBER radius_ NOTIFY changed FINAL)
  Q_PROPERTY(int padding MEMBER padding_ NOTIFY changed FINAL)
  Q_PROPERTY(int spacing MEMBER spacing_ NOTIFY changed FINAL)
  // Glyphs: the font that draws icons (a widget's `icon`) and their size in pixels. The weight of
  // widget text, 100 to 900 (400 normal, 600 semi bold).
  Q_PROPERTY(QString iconFont MEMBER iconFont_ NOTIFY changed FINAL)
  Q_PROPERTY(int iconSize MEMBER iconSize_ NOTIFY changed FINAL)
  Q_PROPERTY(int fontWeight MEMBER fontWeight_ NOTIFY changed FINAL)

  // Islands, the adaptive look of Python YASB: with `islands` on, each non-empty widget group
  // (start, centre, end) of a BarLayout gets its own background, bottom corners rounded by
  // `islandRadius` and concave fillets where it meets the screen edge, and the bar window itself
  // draws no background, so the wallpaper shows between the islands. `groupPadding` is the room
  // around a group inside its island.
  Q_PROPERTY(bool islands MEMBER islands_ NOTIFY changed FINAL)
  Q_PROPERTY(int islandRadius MEMBER islandRadius_ NOTIFY changed FINAL)
  Q_PROPERTY(int groupPadding MEMBER groupPadding_ NOTIFY changed FINAL)

 public:
  QColor barFill() const { return barFill_.isValid() ? barFill_ : fill_; }
  QColor barFillHover() const { return barFillHover_.isValid() ? barFillHover_ : fillHover_; }
  void setBarFill(const QColor& color) {
    barFill_ = color;
    emit changed();
  }
  void setBarFillHover(const QColor& color) {
    barFillHover_ = color;
    emit changed();
  }
  QColor accentFill() const { return withAlpha(accent_, 0x88); }
  QColor warnFill() const { return withAlpha(warn_, 0x55); }
  QColor goodFill() const { return withAlpha(good_, 0x33); }
  QColor cautionFill() const { return withAlpha(caution_, 0x33); }

  // Copies every writable property of `other` into this one (the defaults of a property the theme
  // file did not set come along, so a removed line returns to its default).
  void assignFrom(const ThemeValues& other);

 signals:
  void changed();

 protected:
  explicit ThemeValues(QObject* parent = nullptr);

 private:
  static QColor withAlpha(QColor color, int alpha) {
    color.setAlpha(alpha);
    return color;
  }

  QColor text_{0xff, 0xff, 0xff};
  QColor surface_{0x10, 0x10, 0x18, 0x66};
  QColor accent_{0x5b, 0x8d, 0xef};
  QColor warn_{0xe5, 0x48, 0x4d};
  QColor good_{0x38, 0xc1, 0x72};
  QColor caution_{0xf0, 0xc3, 0x6d};
  QColor fill_{0xff, 0xff, 0xff, 0x22};
  QColor fillHover_{0xff, 0xff, 0xff, 0x44};
  QColor fillActive_{0xff, 0xff, 0xff, 0x55};
  QColor fillUsed_{0xff, 0xff, 0xff, 0x33};
  QColor fillEmpty_{0xff, 0xff, 0xff, 0x18};
  QColor barFill_;       // invalid: follows fill_
  QColor barFillHover_;  // invalid: follows fillHover_
  QColor popupSurface_{0x18, 0x18, 0x20, 0xf0};
  QColor popupBorder_{0xff, 0xff, 0xff, 0x33};
  QString fontFamily_;
  int fontSize_ = 13;
  int smallFontSize_ = 12;
  int pillHeight_ = 24;
  int radius_ = -1;
  int padding_ = 10;
  int spacing_ = 12;
  QString iconFont_{QStringLiteral("Segoe Fluent Icons")};
  int iconSize_ = 16;
  int fontWeight_ = 400;
  bool islands_ = false;
  int islandRadius_ = 16;
  int groupPadding_ = 12;
};

// The root of theme.qml:
//
//     import tidy
//     ThemeSpec { accent: "#ff8800"; fontFamily: "Cascadia Code" }
class ThemeSpec : public ThemeValues {
  Q_OBJECT
  QML_ELEMENT

 public:
  explicit ThemeSpec(QObject* parent = nullptr) : ThemeValues(parent) {}
};

// What widgets read. One per engine; the host fills it from theme.qml before it builds the bars.
class Theme : public ThemeValues {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  explicit Theme(QObject* parent = nullptr) : ThemeValues(parent) {}
};

}  // namespace tidy
