#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

namespace waku::quickshell_io {

// The text of the system clipboard, read lazily: nothing is read until text() is called, and the
// value is cached until the clipboard changes. Quickshell's `clipboardText` reads through it, so
// creating the Quickshell singleton (on every start and reload) does not touch the clipboard. A
// read can block the GUI thread: Qt's Windows clipboard retries OleGetClipboard with sleeps while
// another process holds the clipboard, and an owner that renders on demand can keep
// GetClipboardData waiting for seconds.
//
// textChanged() fires on a clipboard change only after a read, since nothing can depend on a value
// that was never read. Registered as `ClipboardText` in waku.services. GUI thread only.
class ClipboardText : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged FINAL)

 public:
  explicit ClipboardText(QObject* parent = nullptr);

  QString text() const;
  void setText(const QString& text);

  // How often any ClipboardText of this process has read the system clipboard. Tests use it to
  // prove that start and reload do not read it.
  static int readsForTesting();

 signals:
  void textChanged();

 private:
  void invalidate();

  mutable QString cache_;
  mutable bool cached_ = false;
};

}  // namespace waku::quickshell_io
