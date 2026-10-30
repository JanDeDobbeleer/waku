#include "host/qml/shell_screen.h"

#include <QGuiApplication>
#include <QQmlEngine>
#include <vector>

#include "host/screen_match.h"

namespace wabar::host {

ShellScreen::ShellScreen(QScreen* screen, bool primary, QObject* parent)
    : QObject(parent),
      screen_(screen),
      name_(screen ? screen->name() : QString()),
      primary_(primary) {
  if (screen) {
    connect(screen, &QScreen::geometryChanged, this, &ShellScreen::geometryChanged);
    connect(screen, &QScreen::physicalDotsPerInchChanged, this, &ShellScreen::geometryChanged);
  }
}

ScreenList::ScreenList(QObject* parent) : QObject(parent) {
  setScreens(QGuiApplication::screens(), QGuiApplication::primaryScreen());
}

ScreenList* ScreenList::of(QQmlEngine* engine) {
  if (!engine) return nullptr;
  return engine->singletonInstance<ScreenList*>(QStringLiteral("wabar"),
                                                QStringLiteral("ScreenList"));
}

void ScreenList::setScreens(const QList<QScreen*>& screens, QScreen* primary) {
  const QList<ShellScreen*> old = screens_;
  screens_.clear();
  if (!primary && !screens.isEmpty()) primary = screens.first();
  bool primaryTaken = false;
  for (QScreen* screen : screens) {
    // A test may list one QScreen twice; only the first of them is "primary".
    const bool isPrimary = screen == primary && !primaryTaken;
    primaryTaken = primaryTaken || isPrimary;
    screens_ << new ShellScreen(screen, isPrimary, this);
  }
  emit screensChanged();
  // Windows of the previous shell may still point at the old objects until they are destroyed.
  for (ShellScreen* screen : old) screen->deleteLater();
}

ShellScreen* ScreenList::primary() const {
  for (ShellScreen* screen : screens_) {
    if (screen->primary()) return screen;
  }
  return screens_.value(0);
}

ShellScreen* ScreenList::find(QScreen* screen) const {
  for (ShellScreen* candidate : screens_) {
    if (candidate->qscreen() == screen) return candidate;
  }
  return nullptr;
}

QVariantList ScreenList::match(const QStringList& patterns) const {
  std::vector<ScreenRef> refs;
  for (const ShellScreen* screen : screens_) refs.push_back({screen->name(), screen->primary()});
  QVariantList out;
  for (int index : matchScreens(patterns, refs)) {
    out << QVariant::fromValue(static_cast<QObject*>(screens_.at(index)));
  }
  return out;
}

QQmlListProperty<ShellScreen> ScreenList::screensProperty() {
  return QQmlListProperty<ShellScreen>(
      this, &screens_,
      [](QQmlListProperty<ShellScreen>* p) -> qsizetype {
        return static_cast<QList<ShellScreen*>*>(p->data)->size();
      },
      [](QQmlListProperty<ShellScreen>* p, qsizetype i) -> ShellScreen* {
        return static_cast<QList<ShellScreen*>*>(p->data)->value(i);
      });
}

}  // namespace wabar::host
