#include "host/qml/lazy_loader.h"

#include <QDebug>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlIncubationController>

namespace wabar::host {

class LazyLoader::Incubator : public QQmlIncubator {
 public:
  explicit Incubator(LazyLoader* loader) : QQmlIncubator(Asynchronous), loader_(loader) {}

 protected:
  void statusChanged(Status status) override {
    // Never finish inside the incubator's own callback: the loader deletes the incubator.
    if (status == Ready || status == Error) {
      QMetaObject::invokeMethod(
          loader_, [loader = loader_] { loader->finishAsync(); }, Qt::QueuedConnection);
    }
  }

 private:
  LazyLoader* loader_;
};

LazyLoader::LazyLoader(QObject* parent) : QObject(parent) {
  connect(this, &LazyLoader::activeChanged, this, &LazyLoader::activeAsyncChanged);
  connect(this, &LazyLoader::loadingChanged, this, &LazyLoader::activeAsyncChanged);
  drive_.setSingleShot(true);
  drive_.setInterval(0);
  connect(&drive_, &QTimer::timeout, this, [this] {
    if (!incubator_) return;
    incubator_->forceCompletion();
    finishAsync();
  });
}

LazyLoader::~LazyLoader() {
  incubator_.reset();
  delete item_.data();
}

void LazyLoader::setComponent(QQmlComponent* component) {
  if (component_ == component) return;
  component_ = component;
  emit componentChanged();
  if (complete_ && active()) {
    destroyItem();
    createNow();
  }
}

void LazyLoader::setSource(const QUrl& source) {
  if (source_ == source) return;
  source_ = source;
  sourceComponent_.reset();
  emit sourceChanged();
  if (complete_ && active()) {
    destroyItem();
    createNow();
  }
}

void LazyLoader::setLoading(bool loading) {
  wantLoading_ = loading;
  if (!complete_ || !loading || active() || incubator_) return;
  startAsync();
}

void LazyLoader::setActive(bool active) {
  wantActive_ = active;
  if (!complete_) return;
  if (active) {
    if (!this->active()) createNow();
  } else {
    cancel();
    destroyItem();
  }
}

void LazyLoader::setActiveAsync(bool active) {
  if (active) {
    setLoading(true);
    return;
  }
  wantLoading_ = false;
  setActive(false);
}

void LazyLoader::componentComplete() {
  complete_ = true;
  if (wantActive_) {
    createNow();
  } else if (wantLoading_) {
    startAsync();
  }
}

QQmlComponent* LazyLoader::effectiveComponent() {
  if (component_) return component_;
  if (source_.isEmpty()) return nullptr;
  if (!sourceComponent_) {
    QQmlEngine* engine = qmlEngine(this);
    if (!engine) return nullptr;
    const QUrl url = qmlContext(this) ? qmlContext(this)->resolvedUrl(source_) : source_;
    sourceComponent_ = std::make_unique<QQmlComponent>(engine, url);
  }
  return sourceComponent_.get();
}

void LazyLoader::startAsync() {
  QQmlComponent* component = effectiveComponent();
  if (!component) return;
  QQmlContext* context = component->creationContext();
  if (!context) context = qmlContext(this);
  incubator_ = std::make_unique<Incubator>(this);
  emit loadingChanged();
  component->create(*incubator_, context);
  // Without an incubation controller nothing drives the incubator: finish on the next turn of the
  // event loop instead.
  QQmlEngine* engine = qmlEngine(this);
  if (!engine || !engine->incubationController()) drive_.start();
}

void LazyLoader::finishAsync() {
  if (!incubator_) return;
  if (incubator_->isLoading()) return;
  QObject* object = incubator_->isReady() ? incubator_->object() : nullptr;
  if (incubator_->isError()) {
    for (const QQmlError& error : incubator_->errors()) {
      qWarning().noquote() << "wabar: LazyLoader:" << error.toString();
    }
  }
  incubator_.reset();
  emit loadingChanged();
  if (object) adopt(object);
}

void LazyLoader::createNow() {
  if (incubator_) {
    incubator_->forceCompletion();
    finishAsync();
    if (active()) return;
  }
  QQmlComponent* component = effectiveComponent();
  if (!component) return;
  QQmlContext* context = component->creationContext();
  if (!context) context = qmlContext(this);
  QObject* object = component->create(context);
  if (!object) {
    for (const QQmlError& error : component->errors()) {
      qWarning().noquote() << "wabar: LazyLoader:" << error.toString();
    }
    return;
  }
  adopt(object);
}

void LazyLoader::adopt(QObject* object) {
  QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
  object->setParent(this);
  item_ = object;
  emit itemChanged();
  emit activeChanged();
}

void LazyLoader::destroyItem() {
  if (!item_) return;
  QObject* item = item_;
  item_ = nullptr;
  delete item;
  emit itemChanged();
  emit activeChanged();
}

void LazyLoader::cancel() {
  if (!incubator_) return;
  drive_.stop();
  incubator_.reset();
  emit loadingChanged();
}

}  // namespace wabar::host
