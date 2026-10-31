#include "host/qml/ipc_handler.h"

#include <QColor>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QMetaMethod>
#include <QRegularExpression>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

namespace wabar::host {

namespace {

constexpr int kMaxArguments = 10;

QList<IpcHandler*>& handlers() {
  static QList<IpcHandler*> live;
  return live;
}

// QML functions are slots of the dynamic meta object.
bool isFunction(const QMetaMethod& method) {
  return method.methodType() == QMetaMethod::Method || method.methodType() == QMetaMethod::Slot;
}

bool isUntyped(const QMetaType& type) {
  return !type.isValid() || type == QMetaType::fromType<QVariant>() ||
         type == QMetaType::fromType<QJSValue>();
}

QString typeName(const QMetaType& type) {
  if (isUntyped(type)) return QStringLiteral("var");
  if (type == QMetaType::fromType<QString>()) return QStringLiteral("string");
  if (type == QMetaType::fromType<int>()) return QStringLiteral("int");
  if (type == QMetaType::fromType<bool>()) return QStringLiteral("bool");
  if (type == QMetaType::fromType<double>() || type == QMetaType::fromType<float>())
    return QStringLiteral("real");
  if (type == QMetaType::fromType<QColor>()) return QStringLiteral("color");
  return QString::fromLatin1(type.name());
}

QColor parseColor(const QString& text) {
  QColor color = QColor::fromString(text);
  if (color.isValid()) return color;
  static const QRegularExpression hex(
      QStringLiteral("^[0-9a-fA-F]{3}$|^[0-9a-fA-F]{6}$|^[0-9a-fA-F]{8}$"));
  if (hex.match(text).hasMatch()) color = QColor::fromString(QLatin1Char('#') + text);
  return color;
}

// Converts one argument word to the declared type; false with `error` set when it does not parse.
bool convertArgument(const QString& word, const QMetaType& type, QVariant* out, QString* error) {
  if (isUntyped(type) || type == QMetaType::fromType<QString>()) {
    *out = word;
    return true;
  }
  bool ok = false;
  if (type == QMetaType::fromType<int>()) {
    const int value = word.toInt(&ok);
    if (ok) *out = value;
  } else if (type == QMetaType::fromType<bool>()) {
    if (word == QLatin1String("true")) {
      *out = true;
      ok = true;
    } else if (word == QLatin1String("false")) {
      *out = false;
      ok = true;
    } else {
      const int value = word.toInt(&ok);
      if (ok) *out = value != 0;
    }
  } else if (type == QMetaType::fromType<double>()) {
    const double value = word.toDouble(&ok);
    if (ok) *out = value;
  } else if (type == QMetaType::fromType<float>()) {
    const float value = word.toFloat(&ok);
    if (ok) *out = value;
  } else if (type == QMetaType::fromType<QColor>()) {
    const QColor color = parseColor(word);
    ok = color.isValid();
    if (ok) *out = color;
  } else {
    *error = QStringLiteral("unsupported argument type '%1'").arg(typeName(type));
    return false;
  }
  if (!ok) {
    *error = QStringLiteral("cannot read '%1' as %2").arg(word.left(40), typeName(type));
    return false;
  }
  return true;
}

QString formatResult(QVariant value) {
  if (value.metaType() == QMetaType::fromType<QJSValue>()) {
    value = value.value<QJSValue>().toVariant();
  }
  if (!value.isValid() || value.isNull()) return QStringLiteral("ok");
  const QMetaType type = value.metaType();
  if (type == QMetaType::fromType<bool>()) {
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  }
  if (type == QMetaType::fromType<QColor>()) {
    return value.value<QColor>().name(QColor::HexArgb);
  }
  if (type == QMetaType::fromType<QVariantMap>() || type == QMetaType::fromType<QVariantList>() ||
      type == QMetaType::fromType<QStringList>()) {
    return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
  }
  if (type == QMetaType::fromType<QJsonDocument>()) {
    return QString::fromUtf8(value.toJsonDocument().toJson(QJsonDocument::Compact));
  }
  QString text = value.toString();
  // One line: a string with line breaks is printed as a JSON string.
  if (text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r'))) {
    text = QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact));
    text = text.mid(1, text.size() - 2);
  }
  return text;
}

}  // namespace

IpcHandler::IpcHandler(QObject* parent) : QObject(parent) {}

IpcHandler::~IpcHandler() { handlers().removeAll(this); }

void IpcHandler::setTarget(const QString& target) {
  if (target == target_) return;
  target_ = target;
  emit targetChanged();
}

void IpcHandler::setEnabled(bool enabled) {
  if (enabled == enabled_) return;
  enabled_ = enabled;
  emit enabledChanged();
}

void IpcHandler::componentComplete() {
  complete_ = true;
  handlers().removeAll(this);
  handlers().append(this);
}

QStringList IpcHandler::functionSignatures() const {
  QStringList result;
  const QMetaObject* meta = metaObject();
  for (int i = staticMetaObject.methodCount(); i < meta->methodCount(); ++i) {
    const QMetaMethod method = meta->method(i);
    if (!isFunction(method)) continue;
    QStringList params;
    for (int p = 0; p < method.parameterCount(); ++p) {
      params << QStringLiteral("%1 %2").arg(typeName(method.parameterMetaType(p)),
                                            QString::fromUtf8(method.parameterNames().at(p)));
    }
    const QMetaType ret = method.returnMetaType();
    result << QStringLiteral("%1(%2): %3")
                  .arg(QString::fromLatin1(method.name()), params.join(QLatin1String(", ")),
                       ret.id() == QMetaType::Void ? QStringLiteral("void") : typeName(ret));
  }
  return result;
}

QString IpcHandler::list() {
  QStringList parts;
  for (IpcHandler* handler : std::as_const(handlers())) {
    if (!handler->enabled_ || handler->target_.isEmpty()) continue;
    parts << QStringLiteral("%1: %2").arg(handler->target_,
                                          handler->functionSignatures().join(QLatin1String("; ")));
  }
  return parts.isEmpty() ? QStringLiteral("ok: no handlers")
                         : QStringLiteral("ok: ") + parts.join(QLatin1String(" | "));
}

QString IpcHandler::callByName(const QStringList& words) {
  static const QString usage = QStringLiteral("error: usage: call <target> <function> [args...]");
  if (words.isEmpty()) return list();
  if (words.size() < 2) return usage;
  IpcHandler* found = nullptr;
  for (IpcHandler* handler : std::as_const(handlers())) {
    if (handler->enabled_ && handler->target_ == words.first()) found = handler;  // newest wins
  }
  if (!found) {
    return QStringLiteral("error: no handler with target '%1' (try: call)")
        .arg(words.first().left(40));
  }
  return found->invoke(words.at(1), words.mid(2));
}

QString IpcHandler::invoke(const QString& function, const QStringList& arguments) {
  const QMetaObject* meta = metaObject();
  const QByteArray name = function.toUtf8();
  QMetaMethod method;
  for (int i = staticMetaObject.methodCount(); i < meta->methodCount(); ++i) {
    const QMetaMethod candidate = meta->method(i);
    if (isFunction(candidate) && candidate.name() == name) {
      method = candidate;
    }
  }
  if (!method.isValid()) {
    return QStringLiteral("error: '%1' has no function '%2'")
        .arg(target_.left(40), function.left(40));
  }
  const int count = method.parameterCount();
  if (count > kMaxArguments) return QStringLiteral("error: more than 10 arguments");
  if (arguments.size() != count) {
    return QStringLiteral("error: %1 takes %2 argument(s), got %3")
        .arg(function.left(40))
        .arg(count)
        .arg(arguments.size());
  }

  std::vector<QVariant> values(static_cast<size_t>(count));
  std::vector<void*> argv(static_cast<size_t>(count) + 1, nullptr);
  for (int i = 0; i < count; ++i) {
    const QMetaType type = method.parameterMetaType(i);
    QVariant converted;
    QString error;
    if (!convertArgument(arguments.at(i), type, &converted, &error)) {
      return QStringLiteral("error: argument %1: %2").arg(i + 1).arg(error);
    }
    // A QVariant parameter gets a pointer to a QVariant; every other one, QJSValue included, a
    // pointer to a value of exactly its type. Handing a QVariant to a QJSValue parameter would make
    // the callee treat a QVariant as a QJSValue and destroy it as one.
    QVariant& slot = values[static_cast<size_t>(i)];
    const bool variantParameter = !type.isValid() || type == QMetaType::fromType<QVariant>();
    if (variantParameter) {
      slot = converted;
    } else if (type == QMetaType::fromType<QJSValue>()) {
      slot = QVariant::fromValue(QJSValue(converted.toString()));
    } else {
      converted.convert(type);
      slot = converted;
    }
    argv[static_cast<size_t>(i) + 1] = variantParameter ? static_cast<void*>(&slot) : slot.data();
  }

  // The return buffer is a QVariant for `var` and a value of exactly the return type otherwise
  // (a QJSValue when Qt reports one), never a QVariant standing in for another type.
  const QMetaType returnType = method.returnMetaType();
  QVariant result;
  QVariant typedResult;
  if (returnType.isValid() && returnType.id() != QMetaType::Void) {
    if (returnType == QMetaType::fromType<QVariant>()) {
      argv[0] = &result;
    } else {
      typedResult = QVariant(returnType);
      argv[0] = typedResult.data();
    }
  }
  QMetaObject::metacall(this, QMetaObject::InvokeMetaMethod, method.methodIndex(), argv.data());
  if (argv[0] == &result) return formatResult(result);
  if (typedResult.metaType() == QMetaType::fromType<QJSValue>()) {
    return formatResult(typedResult.value<QJSValue>().toVariant());
  }
  return formatResult(typedResult);
}

}  // namespace wabar::host
