#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>

#include "platform/systray_host.h"
#include "spike/services/tray_icon_model.h"
#include "widgets/systray/tray_icon_registry.h"

class QQmlEngine;
class QJSEngine;

namespace tidy::services {

namespace platform = ::yasb::platform;

// The notification-area icons of other programs, as a QML singleton shared by every Tray widget.
// Owns the one TrayHost (the hook in Explorer; there is no legacy mode), applies the
// TrayIconRegistry to its notifications and mirrors the result in `icons`. Registers the
// image://tray provider on the engine that creates it. When no host can be used, `available` is
// false, `unavailableReason` says why and `icons` stays empty; nothing touches Explorer.
//
// Threads: the host's worker thread emits queued signals; everything here is on the GUI thread.
// The destructor stops the host (pipe and mutex are released; the DLL detaches within 5 s).
class Systray : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)
  Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY availableChanged FINAL)
  Q_PROPERTY(QObject* icons READ icons CONSTANT FINAL)

 public:
  // Real: hook host from the DLL next to the executable; unavailable on the offscreen platform.
  // Deliberately not default-constructible: Qt uses a singleton's default constructor in
  // preference to create(), and create() must run to register the image provider.
  explicit Systray(QObject* parent);
  // Test seam: any host (null = unavailable with `unavailableReason`) and window API.
  Systray(std::unique_ptr<platform::TrayHost> host, std::unique_ptr<platform::TrayWindowApi> api,
          QString unavailableReason, QObject* parent = nullptr);
  ~Systray() override;

  // QML_SINGLETON factory: builds the real one and registers the image provider on `engine`.
  static Systray* create(QQmlEngine* engine, QJSEngine*);
  // Tests: create() builds the singleton with this factory (called once per create) instead of
  // the real host. An empty function restores the real path.
  static void setFactoryForTests(std::function<std::unique_ptr<Systray>()> factory);

  bool available() const { return host_ != nullptr; }
  QString unavailableReason() const { return reason_; }
  TrayIconModel* icons() { return &model_; }
  yasb::widgets::TrayIconRegistry& registry() { return registry_; }

  // Windows' own battery, volume and network icons (hidden by default) and icons of these
  // executables are not admitted. Icons already on screen are not re-filtered.
  Q_INVOKABLE void setFilter(bool showSystemIcons, const QStringList& hiddenExeNames);

  // A full click: press and release to the program, then the version 3+ notifications. `button`
  // is Qt::LeftButton, Qt::RightButton or Qt::MiddleButton. Unknown ids are ignored.
  Q_INVOKABLE void click(int id, int button);
  // WM_LBUTTONDBLCLK; the release that follows from the same gesture is swallowed.
  Q_INVOKABLE void doubleClick(int id);
  // Asks Explorer to re-announce its icons (TaskbarCreated) and drops icons whose window is gone.
  Q_INVOKABLE void refresh();

 signals:
  void availableChanged();

 private:
  void onIconModified(const platform::TrayIconData& data);
  void onIconDeleted(const platform::TrayIconData& data);
  void sweep();
  void connectHost();

  std::unique_ptr<platform::TrayHost> host_;
  std::unique_ptr<platform::TrayWindowApi> api_;
  QString reason_;
  yasb::widgets::TrayIconRegistry registry_;
  TrayIconModel model_;
  // Ids whose next left-button release is swallowed: set by doubleClick (the release of the
  // double-click gesture must not act as a second click), cleared by the next left click.
  QSet<int> swallowRelease_;
  QTimer sweepTimer_;  // 5 s, as the Widgets build: drops icons whose owner window is gone
};

}  // namespace tidy::services
