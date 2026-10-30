#include "features/quickshell_io/qml/data_stream_parsers.h"

#include <utility>

namespace wabar::quickshell_io {

void StdioCollector::setWaitForEnd(bool wait) {
  if (wait == waitForEnd_) return;
  waitForEnd_ = wait;
  emit waitForEndChanged();
}

void StdioCollector::feed(const QByteArray& chunk) {
  buffer_.append(chunk);
  if (!waitForEnd_) publish();
}

void StdioCollector::streamEnded() {
  publish();
  emit streamFinished();
}

void StdioCollector::reset() {
  buffer_.clear();
  publish();
}

void StdioCollector::publish() {
  if (buffer_ != data_) {
    data_ = buffer_;
    emit dataChanged();
  }
  const QString text = QString::fromUtf8(data_);
  if (text != text_) {
    text_ = text;
    emit textChanged();
  }
}

void SplitParser::setSplitMarker(const QString& marker) {
  if (marker == marker_) return;
  marker_ = marker;
  emit splitMarkerChanged();
}

void SplitParser::emitPiece(const QByteArray& piece) {
  QByteArray out = piece;
  if (marker_ == QLatin1String("\n") && out.endsWith('\r')) out.chop(1);
  emit read(QString::fromUtf8(out));
}

void SplitParser::feed(const QByteArray& chunk) {
  if (marker_.isEmpty()) {
    if (!chunk.isEmpty()) emit read(QString::fromUtf8(chunk));
    return;
  }
  buffer_.append(chunk);
  const QByteArray marker = marker_.toUtf8();
  qsizetype start = 0;
  for (;;) {
    const qsizetype at = buffer_.indexOf(marker, start);
    if (at < 0) break;
    emitPiece(buffer_.mid(start, at - start));
    start = at + marker.size();
  }
  buffer_.remove(0, start);
}

void SplitParser::streamEnded() {
  if (!buffer_.isEmpty()) {
    const QByteArray rest = std::exchange(buffer_, QByteArray());
    emitPiece(rest);
  }
}

}  // namespace wabar::quickshell_io
