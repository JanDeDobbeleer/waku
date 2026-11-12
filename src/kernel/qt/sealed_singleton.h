#pragma once

#include <QObject>

namespace waku::kernel {

// Qt 6.12's qqmljsshadowcheck.cpp checkShadowing treats C++ singleton methods as shadowable unless
// they have an extension. This empty extension prevents that; its proxy metaobject adds overhead to
// every property access, so only singletons with QML-callable methods should use it.
class SealedSingleton : public QObject {
  Q_OBJECT

 public:
  explicit SealedSingleton(QObject* parent = nullptr) : QObject(parent) {}
};

}  // namespace waku::kernel