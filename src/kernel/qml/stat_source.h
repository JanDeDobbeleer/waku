#pragma once

#include <QtQml/qqmlregistration.h>

#include <QQmlParserStatus>

#include "kernel/qt/polled_source.h"

namespace wabar::kernel {

// The QML face of PolledSource: polling starts when the QML component is complete. Base of
// the polled data sources (Memory, Cpu, Battery, Network).
class StatSource : public PolledSource, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_ANONYMOUS

 public:
  explicit StatSource(QObject* parent = nullptr) : PolledSource(parent) {}

  void classBegin() override {}
  void componentComplete() override { begin(); }
};

}  // namespace wabar::kernel
