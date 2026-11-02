#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlIncubator>
#include <QQmlParserStatus>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace waku::host {

// Creates its component only when asked (Quickshell `LazyLoader`): a launcher or an OSD window
// that should not exist, or build its widgets, until it is first needed.
//
//     LazyLoader {
//         id: launcher
//         loading: true             // build it in the background after start-up
//         PanelWindow { ... }
//     }
//     // launcher.active = true shows it now; false destroys it.
//
//   component  what to create (the default property); or `source`, a QML file
//   loading    true starts an asynchronous build; it turns false and `active` true when done
//   active     true builds now (finishing a running asynchronous build); false destroys the item
//              (hidden at once, deleted on the next turn of the event loop)
//   activeAsync  true starts an asynchronous build, false destroys the item; reads true while a
//              build runs and once the item exists
//   item       the created object, or null; owned by the loader
//
// Threading: GUI thread only. An asynchronous build runs on the engine's incubation controller,
// or on a zero-delay timer when the engine has none.
class LazyLoader : public QObject, public QQmlParserStatus {
  Q_OBJECT
  Q_INTERFACES(QQmlParserStatus)
  QML_ELEMENT
  Q_CLASSINFO("DefaultProperty", "component")
  Q_PROPERTY(
      QQmlComponent* component READ component WRITE setComponent NOTIFY componentChanged FINAL)
  Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged FINAL)
  Q_PROPERTY(bool loading READ loading WRITE setLoading NOTIFY loadingChanged FINAL)
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
  Q_PROPERTY(bool activeAsync READ activeAsync WRITE setActiveAsync NOTIFY activeAsyncChanged FINAL)
  Q_PROPERTY(QObject* item READ item NOTIFY itemChanged FINAL)

 public:
  explicit LazyLoader(QObject* parent = nullptr);
  ~LazyLoader() override;

  QQmlComponent* component() const { return component_; }
  void setComponent(QQmlComponent* component);
  QUrl source() const { return source_; }
  void setSource(const QUrl& source);
  bool loading() const { return incubator_ != nullptr; }
  void setLoading(bool loading);
  bool active() const { return item_ != nullptr; }
  void setActive(bool active);
  bool activeAsync() const { return active() || loading(); }
  void setActiveAsync(bool active);
  QObject* item() const { return item_; }

  void classBegin() override {}
  void componentComplete() override;

 signals:
  void componentChanged();
  void sourceChanged();
  void loadingChanged();
  void activeChanged();
  void activeAsyncChanged();
  void itemChanged();

 private:
  class Incubator;

  QQmlComponent* effectiveComponent();
  void startAsync();
  void finishAsync();
  void createNow();
  void adopt(QObject* object);
  void destroyItem();
  // Takes `item` off the screen now and deletes it later (see lazy_loader.cpp).
  static void dispose(QObject* item);
  void cancel();

  QPointer<QQmlComponent> component_;
  std::unique_ptr<QQmlComponent> sourceComponent_;
  QUrl source_;
  QPointer<QObject> item_;
  std::unique_ptr<Incubator> incubator_;
  QTimer drive_;
  bool complete_ = false;
  bool wantActive_ = false;
  bool wantLoading_ = false;
};

}  // namespace waku::host
