#pragma once

#include <QtQml/qqmlregistration.h>

#include <QByteArray>
#include <QObject>
#include <QString>

namespace wabar::quickshell_io {

// Quickshell.Io DataStreamParser: what a Process feeds its stdout or stderr into. The process calls
// feed() for each chunk and streamEnded() once, when the stream closes. GUI thread only.
class DataStreamParser : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("DataStreamParser is a base type; use StdioCollector or SplitParser")

 public:
  explicit DataStreamParser(QObject* parent = nullptr) : QObject(parent) {}

  virtual void feed(const QByteArray& chunk) = 0;
  virtual void streamEnded() {}
  // Called when the process starts, so one parser serves many runs.
  virtual void reset() {}

 signals:
  // One delimited chunk of the stream (SplitParser emits it).
  void read(const QString& data);
};

// Collects the whole stream. With waitForEnd (the default) `text` and `data` change once, when the
// stream ends; without it they grow as data arrives. streamFinished() is emitted at the end.
class StdioCollector : public DataStreamParser {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString text READ text NOTIFY textChanged FINAL)
  Q_PROPERTY(QByteArray data READ data NOTIFY dataChanged FINAL)
  Q_PROPERTY(bool waitForEnd READ waitForEnd WRITE setWaitForEnd NOTIFY waitForEndChanged FINAL)

 public:
  explicit StdioCollector(QObject* parent = nullptr) : DataStreamParser(parent) {}

  QString text() const { return text_; }
  QByteArray data() const { return data_; }
  bool waitForEnd() const { return waitForEnd_; }
  void setWaitForEnd(bool wait);

  void feed(const QByteArray& chunk) override;
  void streamEnded() override;
  void reset() override;

 signals:
  void textChanged();
  void dataChanged();
  void waitForEndChanged();
  void streamFinished();

 private:
  void publish();

  QByteArray buffer_;
  QByteArray data_;
  QString text_;
  bool waitForEnd_ = true;
};

// Splits the stream at `splitMarker` and emits read(line) for each piece. A trailing piece without
// a marker is emitted when the stream ends. With the default "\n" marker a trailing "\r" is
// dropped (Windows tools end lines with "\r\n"). An empty marker emits each chunk as it comes.
class SplitParser : public DataStreamParser {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(
      QString splitMarker READ splitMarker WRITE setSplitMarker NOTIFY splitMarkerChanged FINAL)

 public:
  explicit SplitParser(QObject* parent = nullptr) : DataStreamParser(parent) {}

  QString splitMarker() const { return marker_; }
  void setSplitMarker(const QString& marker);

  void feed(const QByteArray& chunk) override;
  void streamEnded() override;
  void reset() override { buffer_.clear(); }

 signals:
  void splitMarkerChanged();

 private:
  void emitPiece(const QByteArray& piece);

  QByteArray buffer_;
  QString marker_ = QStringLiteral("\n");
};

}  // namespace wabar::quickshell_io
