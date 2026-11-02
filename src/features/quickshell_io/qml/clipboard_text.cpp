#include "features/quickshell_io/qml/clipboard_text.h"

#include <QClipboard>
#include <QGuiApplication>
#include <atomic>

namespace waku::quickshell_io {

namespace {

std::atomic<int> clipboardReads{0};

}  // namespace

ClipboardText::ClipboardText(QObject* parent) : QObject(parent) {
  // Connecting reads nothing; the first text() does.
  if (QClipboard* clipboard = QGuiApplication::clipboard())
    connect(clipboard, &QClipboard::dataChanged, this, &ClipboardText::invalidate);
}

QString ClipboardText::text() const {
  if (!cached_) {
    const QClipboard* clipboard = QGuiApplication::clipboard();
    cache_ = clipboard ? clipboard->text() : QString();
    cached_ = true;
    clipboardReads.fetch_add(1, std::memory_order_relaxed);
  }
  return cache_;
}

void ClipboardText::setText(const QString& text) {
  if (cached_ && cache_ == text) return;
  QClipboard* clipboard = QGuiApplication::clipboard();
  if (!clipboard) return;
  // May emit dataChanged() right away (invalidate() then runs first) or later from the platform.
  clipboard->setText(text);
  cache_ = text;
  cached_ = true;
  emit textChanged();
}

void ClipboardText::invalidate() {
  if (!cached_) return;
  cached_ = false;
  cache_.clear();
  emit textChanged();
}

int ClipboardText::readsForTesting() { return clipboardReads.load(std::memory_order_relaxed); }

}  // namespace waku::quickshell_io
