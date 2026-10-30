#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QQmlParserStatus>
#include <QString>
#include <QStringList>

namespace wabar::host {

// Quickshell's `IpcHandler`: functions declared in QML that `wabarc call` runs.
//
//     IpcHandler {
//         target: "launcher"
//         function open(query: string): string { launcher.show(query); return "ok" }
//         function setSize(w: int, h: int): void { size = Qt.size(w, h) }
//     }
//
//     wabarc call launcher open "term"
//
//   target   the name `wabarc call` addresses; required, unique (the newest enabled handler wins,
//            so a reload's new shell answers while the old one is still alive)
//   enabled  false: the handler is not reachable (default true)
//
// Every function declared on the handler is callable. Argument types: string, int, bool ("true",
// "false" or an integer), real and color (named, or hex RGB, RRGGBB, AARRGGBB with an optional #);
// an untyped argument gets the string. At most 10 arguments. Return types: void ("ok"), string,
// int, bool ("true" or "false"), real, color (#AARRGGBB). A `var` result that is a list or an
// object is printed as one line of JSON.
//
// Trust: the command comes through the user-only control pipe, like `shortcut`. Threading: GUI
// thread only.
class IpcHandler : public QObject, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_ELEMENT
  Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY targetChanged FINAL)
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)

 public:
  explicit IpcHandler(QObject* parent = nullptr);
  ~IpcHandler() override;

  QString target() const { return target_; }
  void setTarget(const QString& target);
  bool enabled() const { return enabled_; }
  void setEnabled(bool enabled);

  void classBegin() override {}
  void componentComplete() override;

  // The functions of this handler as "name(type arg, ...): type".
  QStringList functionSignatures() const;

  // `wabarc call`: words are target, function and the arguments. The one-line answer is the
  // return value ("ok" for void), or "error: <text>".
  static QString callByName(const QStringList& words);
  // The reachable targets and their functions, one string, for `wabarc call` without arguments.
  static QString list();

 signals:
  void targetChanged();
  void enabledChanged();

 private:
  QString invoke(const QString& function, const QStringList& arguments);

  QString target_;
  bool enabled_ = true;
  bool complete_ = false;
};

}  // namespace wabar::host
