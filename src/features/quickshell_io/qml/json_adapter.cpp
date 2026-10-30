#include "features/quickshell_io/qml/json_adapter.h"

#include <QDebug>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaProperty>

namespace wabar::quickshell_io {

namespace {

JsonObject* nestedObject(const QVariant& value) {
  if (!value.metaType().flags().testFlag(QMetaType::PointerToQObject)) return nullptr;
  return qobject_cast<JsonObject*>(value.value<QObject*>());
}

QJsonObject objectToJson(const QObject* object, int firstProperty) {
  QJsonObject out;
  const QMetaObject* meta = object->metaObject();
  for (int i = firstProperty; i < meta->propertyCount(); ++i) {
    const QMetaProperty property = meta->property(i);
    QVariant value = property.read(object);
    if (const auto* nested = nestedObject(value)) {
      out.insert(QString::fromLatin1(property.name()), nested->toJson());
      continue;
    }
    if (value.metaType() == QMetaType::fromType<QJSValue>()) {
      value = value.value<QJSValue>().toVariant();
    }
    out.insert(QString::fromLatin1(property.name()), QJsonValue::fromVariant(value));
  }
  return out;
}

void objectFromJson(QObject* object, int firstProperty, const QJsonObject& json) {
  const QMetaObject* meta = object->metaObject();
  for (int i = firstProperty; i < meta->propertyCount(); ++i) {
    const QMetaProperty property = meta->property(i);
    const auto it = json.constFind(QString::fromLatin1(property.name()));
    if (it == json.constEnd()) continue;
    const QVariant current = property.read(object);
    if (auto* nested = nestedObject(current)) {
      if (it->isObject()) nested->fromJson(it->toObject());
      continue;
    }
    const QVariant incoming = it->toVariant();
    if (current == incoming) continue;
    if (!property.write(object, incoming)) {
      qWarning() << "JsonAdapter: cannot assign" << incoming << "to" << property.name();
    }
  }
}

}  // namespace

void PropertyRelay::attach(QObject* target, int firstProperty) {
  const QMetaObject* meta = target->metaObject();
  const int slot = staticMetaObject.indexOfSlot("onPropertyChanged()");
  for (int i = firstProperty; i < meta->propertyCount(); ++i) {
    const QMetaProperty property = meta->property(i);
    if (property.hasNotifySignal()) {
      QMetaObject::connect(target, property.notifySignalIndex(), this, slot);
    }
    if (auto* nested = nestedObject(property.read(target))) {
      connect(nested->relay(), &PropertyRelay::touched, this, &PropertyRelay::touched,
              Qt::UniqueConnection);
    }
  }
}

JsonObject::JsonObject(QObject* parent) : QObject(parent) {}

QJsonObject JsonObject::toJson() const {
  return objectToJson(this, JsonObject::staticMetaObject.propertyCount());
}

void JsonObject::fromJson(const QJsonObject& object) {
  objectFromJson(this, JsonObject::staticMetaObject.propertyCount(), object);
}

void JsonObject::componentComplete() {
  relay_.attach(this, JsonObject::staticMetaObject.propertyCount());
}

JsonAdapter::JsonAdapter(QObject* parent) : FileViewAdapter(parent) {
  connect(&relay_, &PropertyRelay::touched, this, &FileViewAdapter::adapterUpdated);
}

QByteArray JsonAdapter::serialize() {
  return QJsonDocument(objectToJson(this, JsonAdapter::staticMetaObject.propertyCount()))
      .toJson(QJsonDocument::Indented);
}

void JsonAdapter::deserialize(const QByteArray& data) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(data, &error);
  if (!document.isObject()) {
    qWarning() << "JsonAdapter: the file is not a JSON object:" << error.errorString();
    return;
  }
  objectFromJson(this, JsonAdapter::staticMetaObject.propertyCount(), document.object());
}

void JsonAdapter::componentComplete() {
  relay_.attach(this, JsonAdapter::staticMetaObject.propertyCount());
}

}  // namespace wabar::quickshell_io
