#pragma once

#include <QtQml/qqmlregistration.h>

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QQmlParserStatus>

namespace wabar::quickshell_io {

// Quickshell.Io FileViewAdapter: what a FileView converts its file to and from. A FileView calls
// deserialize() after each successful load and serialize() in writeAdapter(). The adapter emits
// adapterUpdated() when QML changes one of its values. GUI thread only.
class FileViewAdapter : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("FileViewAdapter is a base type; use JsonAdapter")

 public:
  explicit FileViewAdapter(QObject* parent = nullptr) : QObject(parent) {}

  virtual QByteArray serialize() = 0;
  virtual void deserialize(const QByteArray& data) = 0;

 signals:
  void adapterUpdated();
};

// Forwards the change notifications of the dynamic (QML-declared) properties of one object, and
// of the JsonObjects below it, as touched(). Internal helper of JsonObject and JsonAdapter.
class PropertyRelay : public QObject {
  Q_OBJECT

 public:
  explicit PropertyRelay(QObject* parent = nullptr) : QObject(parent) {}
  // Connects the notify signal of every property of `target` declared after `firstProperty`, and
  // the relay of every JsonObject held in such a property. Call once, when the object is complete.
  void attach(QObject* target, int firstProperty);

 signals:
  void touched();

 private slots:
  void onPropertyChanged() { emit touched(); }
};

// A nested object of a JsonAdapter: `property JsonObject sub: JsonObject { property int n }`.
// Its QML-declared properties map to the keys of a JSON object.
class JsonObject : public QObject, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_ELEMENT

 public:
  explicit JsonObject(QObject* parent = nullptr);

  PropertyRelay* relay() { return &relay_; }
  QJsonObject toJson() const;
  void fromJson(const QJsonObject& object);

  void classBegin() override {}
  void componentComplete() override;

 private:
  PropertyRelay relay_;
};

// Quickshell.Io JsonAdapter: the properties declared on it in QML (int, bool, string, real, var,
// list<string>, JsonObject) are the keys of a JSON object. Loading updates the properties whose
// value changed; changing one from QML emits adapterUpdated().
class JsonAdapter : public FileViewAdapter, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_ELEMENT

 public:
  explicit JsonAdapter(QObject* parent = nullptr);

  QByteArray serialize() override;
  void deserialize(const QByteArray& data) override;

  void classBegin() override {}
  void componentComplete() override;

 private:
  PropertyRelay relay_;
};

}  // namespace wabar::quickshell_io
