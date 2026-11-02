#include "features/quickshell_io/qml/data_stream_parsers.h"

#include <utility>

namespace waku::quickshell_io {

void StdioCollector::setWaitForEnd(bool wait) {
  if (wait == waitForEnd_) return;
  waitForEnd_ = wait;
  emit waitForEndChanged();
}

void StdioCollector::feed(const QByteArray& chunk) {
  if (waitForEnd_) {
    pending_.append(chunk);
    return;
  }
  // What came while waitForEnd was set, then this chunk.
  if (!pending_.isEmpty()) publish(std::exchange(pending_, QByteArray()));
  publish(chunk);
}

void StdioCollector::streamEnded() {
  publish(std::exchange(pending_, QByteArray()));
  // Bytes of an unfinished UTF-8 sequence wait in the decoder; at the end they are shown the way
  // QString::fromUtf8 shows them.
  decoder_.resetState();
  const QString text = QString::fromUtf8(data_);
  if (text != text_) {
    text_ = text;
    emit textChanged();
  }
  emit streamFinished();
}

void StdioCollector::reset() {
  pending_.clear();
  decoder_.resetState();
  if (!data_.isEmpty()) {
    data_.clear();
    emit dataChanged();
  }
  if (!text_.isEmpty()) {
    text_.clear();
    emit textChanged();
  }
}

void StdioCollector::publish(const QByteArray& bytes) {
  if (bytes.isEmpty()) return;
  data_.append(bytes);
  emit dataChanged();
  const QString text = decoder_.decode(bytes);
  if (!text.isEmpty()) {
    text_.append(text);
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

}  // namespace waku::quickshell_io
