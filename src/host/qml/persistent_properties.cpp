#include "host/qml/persistent_properties.h"

#include <QHash>
#include <QJSValue>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQmlEngine>
#include <QSet>
#include <algorithm>
#include <optional>

namespace waku::host {

namespace {

using Key = std::pair<const QQmlEngine*, QString>;

// The last values of every id, per engine. Objects write here whenever a declared property
// changes, so nothing is read from an object while it is destroyed. Only plain values are kept
// (see snapshot()): a QJSValue is rooted in the engine heap, and the store outlives the engine.
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

// True for a value the store may keep: no QObject pointer and no JS value, also not nested in a
// list or map (QJSValue::toVariant() keeps a function or a QObject wrapper as such).
bool isPlainValue(const QVariant& value) {
  const QMetaType type = value.metaType();
  if (type.flags().testFlag(QMetaType::PointerToQObject)) return false;
  if (type == QMetaType::fromType<QJSValue>()) return false;
  if (type == QMetaType::fromType<QVariantList>()) {
    const QVariantList list = value.toList();
    return std::all_of(list.begin(), list.end(), isPlainValue);
  }
  if (type == QMetaType::fromType<QVariantMap>()) {
    const QVariantMap map = value.toMap();
    return std::all_of(map.begin(), map.end(), isPlainValue);
  }
  return true;
}

}  // namespace

PersistentProperties::PersistentProperties(QObject* parent) : QObject(parent) {}

void PersistentProperties::setReloadableId(const QString& id) {
  if (id == reloadableId_) return;
  reloadableId_ = id;
  emit reloadableIdChanged();
}

std::optional<QVariant> PersistentProperties::plainValue(const QMetaProperty& property) const {
  QVariant value = property.read(this);
  // A `property var` reads as a QJSValue: a persistent value in this engine's heap. Keep its
  // plain conversion (numbers, strings, lists, maps) so nothing in the process-wide store pins a JS
  // object between reloads or is freed after the engine that owns it.
  if (value.metaType() == QMetaType::fromType<QJSValue>()) {
    value = value.value<QJSValue>().toVariant();
  }
  if (!isPlainValue(value)) return std::nullopt;
  return value;
}

QVariantMap PersistentProperties::snapshot() const {
  QVariantMap values;
  const QMetaObject* meta = metaObject();
  for (int i = staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
    const QMetaProperty property = meta->property(i);
    if (const std::optional<QVariant> value = plainValue(property))
      values.insert(QString::fromLatin1(property.name()), *value);
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
  auto& saved = store();
  const Key key{engine_, reloadableId_};
  const auto entry = saved.find(key);
  // A change signal updates only the properties it notifies: an animated value saves itself at
  // frame rate, and a snapshot read every declared property each time. A direct call (completion)
  // or an id without an entry yet writes the whole snapshot.
  const auto changed = notifiedProperties_.constFind(senderSignalIndex());
  if (entry == saved.end() || changed == notifiedProperties_.cend()) {
    saved.insert(key, snapshot());
    return;
  }
  const QMetaObject* meta = metaObject();
  for (const int index : *changed) {
    const QMetaProperty property = meta->property(index);
    const QString name = QString::fromLatin1(property.name());
    if (const std::optional<QVariant> value = plainValue(property)) {
      entry->insert(name, *value);
    } else {
      entry->remove(name);
    }
  }
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
    if (!notify.isValid()) continue;
    QList<int>& properties = notifiedProperties_[notify.methodIndex()];
    if (properties.isEmpty()) QMetaObject::connect(this, notify.methodIndex(), this, slot);
    properties.append(i);
  }
  save();
  if (restored) emit reloaded();
  emit loaded();
}

}  // namespace waku::host
