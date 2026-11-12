#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>

namespace waku::quickshell {

// Quickshell's QsMenuButtonType: the button a menu entry draws. Windows tray menus are drawn by
// the program that owns them, so no entry reaches a shell; tray menus written for Quickshell
// still compare an entry's `buttonType` against these values.
namespace QsMenuButtonType {
Q_NAMESPACE
QML_ELEMENT
enum Enum { None = 0, CheckBox = 1, RadioButton = 2 };
Q_ENUM_NS(Enum)
}  // namespace QsMenuButtonType

}  // namespace waku::quickshell
