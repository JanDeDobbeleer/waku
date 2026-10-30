#include "host/qml/persistent_properties.h"

#include <QHash>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQmlEngine>
#include <QSet>

namespace wabar::host {

namespace {

using Key = std::pair<const QQmlEngine*, QString>;

// The last values of every id, per engine. Objects write here whenever a declared property
// changes, so nothing is read from an object while it is destroyed.
QHash<Key, QVariantMap>& store() {
  static QHash<Key, QVariantMap> saved;
  return saved;
}

// Forgets an engine's entries when it goes, so a later engine at the same address starts empty.
void watchEngine(const QQmlEngine* engine) {
  static QSet<const QQmlEngine*> watched;
  if (!engine || watched.contains(engine)) return;
  watched.insert(engine);
  QObject::connect(engine, &QObject::destroyed, [engine] {
    watched.remove(engine);
    auto& saved = store();
    for (auto it = saved.begin(); it != saved.end();) {
      it = it.key().first == engine ? saved.erase(it) : std::next(it);
    }
  });
}

}  // namespace

PersistentProperties::PersistentProperties(QObject* parent) : QObject(parent) {}

void PersistentProperties::setReloadableId(const QString& id) {
  if (id == reloadableId_) return;
  reloadableId_ = id;
  emit reloadableIdChanged();
}

QVariantMap PersistentProperties::snapshot() const {
  QVariantMap values;
  const QMetaObject* meta = metaObject();
  for (int i = staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
    const QMetaProperty property = meta->property(i);
    const QVariant value = property.read(this);
    if (value.metaType().flags().testFlag(QMetaType::PointerToQObject)) continue;
    values.insert(QString::fromLatin1(property.name()), value);
  }
  return values;
}

void PersistentProperties::restore(const QVariantMap& values) {
  const QMetaObject* meta = metaObject();
  for (int i = staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
    const QMetaProperty property = meta->property(i);
    if (!property.isWritable()) continue;
    const auto it = values.constFind(QString::fromLatin1(property.name()));
    if (it != values.constEnd()) property.write(this, *it);
  }
}

void PersistentProperties::save() {
  if (!complete_ || reloadableId_.isEmpty()) return;
  store().insert(Key{engine_, reloadableId_}, snapshot());
}

void PersistentProperties::componentComplete() {
  complete_ = true;
  if (reloadableId_.isEmpty()) {
    emit loaded();
    return;
  }
  engine_ = qmlEngine(this);
  watchEngine(engine_);
  bool restored = false;
  // A copy: writing the properties below saves them again, one at a time.
  const auto it = store().constFind(Key{engine_, reloadableId_});
  if (it != store().constEnd()) {
    restore(QVariantMap(*it));
    restored = true;
  }
  // From here on every change of a declared property is saved.
  const QMetaObject* meta = metaObject();
  const int slot = staticMetaObject.indexOfSlot("save()");
  for (int i = staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
    const QMetaMethod notify = meta->property(i).notifySignal();
    if (notify.isValid()) QMetaObject::connect(this, notify.methodIndex(), this, slot);
  }
  save();
  if (restored) emit reloaded();
  emit loaded();
}

}  // namespace wabar::host
