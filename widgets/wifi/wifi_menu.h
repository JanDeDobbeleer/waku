#pragma once

#include <QCheckBox>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "platform/wlan.h"
#include "widgets/menu_support/elided_label.h"
#include "widgets/menu_support/loader_line.h"
#include "widgets/menu_support/menu_popup.h"

namespace yasb::widgets {

struct WifiMenuOptions {
  bool blur = true;
  bool roundCorners = true;
  QString roundCornersType = QStringLiteral("normal");
  QString borderColor = QStringLiteral("System");
  QString alignment = QStringLiteral("right");
  QString direction = QStringLiteral("down");
  int offsetLeft = 0;
  int offsetTop = 6;
  QStringList iconsSecured;
  QStringList iconsUnsecured;
};

// wifi_widgets.ClickableLabel
class ClickableLabel : public QLabel {
  Q_OBJECT
 public:
  explicit ClickableLabel(QWidget* parent = nullptr) : QLabel(parent) {}
  bool clickable() const { return clickable_; }
  void setClickable(bool value);

 signals:
  void clicked();

 protected:
  void mousePressEvent(QMouseEvent* event) override;

 private:
  bool clickable_ = false;
};

// wifi_widgets.WifiItem: one network row with an expandable connect area.
class WifiItem : public QFrame {
  Q_OBJECT
 public:
  explicit WifiItem(QWidget* parent = nullptr);

  const platform::WifiNetwork& data() const { return data_; }
  void setData(const platform::WifiNetwork& data);
  bool active() const { return active_; }
  void setActive(bool active) {
    active_ = active;
    updateState();
  }
  // update_state(set_active): only overrides the active flag when `setActive` is given.
  void updateState(std::optional<bool> setActive = std::nullopt);

  QLabel* statusLabel() const { return statusLabel_; }
  QLabel* strengthLabel() const { return strengthLabel_; }
  QLabel* iconLabel() const { return icon_; }
  ElidedLabel* nameLabel() const { return name_; }
  QLineEdit* passwordField() const { return password_; }
  QLineEdit* ssidField() const { return ssid_; }
  QPushButton* connectButton() const { return connect_; }
  QFrame* controlsContainer() const { return controls_; }

 signals:
  void clicked();
  void connectPressed(const yasb::platform::WifiNetwork& network, const QString& password,
                      const QString& ssid);
  void disconnectPressed(const yasb::platform::WifiNetwork& network);
  void forgetPressed(const yasb::platform::WifiNetwork& network);
  void autoConnectToggled(const yasb::platform::WifiNetwork& network);

 protected:
  void mousePressEvent(QMouseEvent* event) override;

 private:
  void showContextMenu(const QPoint& pos);
  void manageConnectionClick();
  void toggleAutoConnect();

  bool active_ = false;
  platform::WifiNetwork data_;
  int state_ = platform::kWifiSecured;
  QFrame* details_;
  QFrame* controls_;
  QLabel* icon_;
  ElidedLabel* name_;
  QLabel* statusLabel_;
  QLabel* strengthLabel_;
  QLineEdit* ssid_;
  QLineEdit* password_;
  QPushButton* connect_;
  QPointer<QCheckBox> autoConnectBox_;
};

// wifi_widgets.WifiList: scrolling list of WifiItem keyed by SSID.
class WifiList : public QScrollArea {
  Q_OBJECT
 public:
  explicit WifiList(QWidget* parent = nullptr);

  void addItem(WifiItem* item);
  // Modifies the existing item in place, or adds a new one with `activeIfNew`.
  void updateOrAddItem(const platform::WifiNetwork& network, bool activeIfNew);
  void removeItem(const QString& ssid);
  WifiItem* item(const QString& ssid) const;
  const std::vector<WifiItem*>& items() const { return items_; }
  void sortItems();
  void clearItems();
  void clearFields();
  void activateItem(const QString& name);
  void onItemClicked(WifiItem* selected);

 signals:
  void connectPressed(const yasb::platform::WifiNetwork& network, const QString& password,
                      const QString& ssid);
  void disconnectPressed(const yasb::platform::WifiNetwork& network);
  void forgetPressed(const yasb::platform::WifiNetwork& network);
  void autoConnectToggled(const yasb::platform::WifiNetwork& network);

 private:
  QFrame* container_;
  QVBoxLayout* layout_;
  std::vector<WifiItem*> items_;
};

// wifi_widgets.WifiMenu: the popup. The Wi-Fi backend is created on first use.
class WifiMenu : public QObject {
  Q_OBJECT
 public:
  using BackendFactory = std::function<std::unique_ptr<platform::WifiBackend>()>;
  using UriOpener = std::function<void(const QString&)>;

  WifiMenu(QWidget* anchor, WifiMenuOptions options, BackendFactory backendFactory = {});
  ~WifiMenu() override;

  void showMenu();
  // Replaces the opener for ms-settings: URIs (tests never launch Settings).
  void setUriOpener(UriOpener opener) { opener_ = std::move(opener); }

  MenuPopup* popup() const { return popup_; }
  WifiList* list() const { return list_; }
  LoaderLine* progressBar() const { return progress_; }
  ClickableLabel* errorMessage() const { return error_; }
  platform::WifiBackend* backend();
  const std::vector<platform::WifiNetwork>& cache() const { return cache_; }

 private:
  void scanWifi();
  void requestListUpdate();
  void updateItemsList(bool connected, const platform::WifiNetwork& active);
  void onScanCompleted(int status, const QVector<platform::WifiNetwork>& networks);
  void onConnect(const platform::WifiNetwork& network, const QString& password,
                 const QString& ssid);
  void onDisconnect(const platform::WifiNetwork& network);
  void onForget(const platform::WifiNetwork& network);
  void onAutoConnectToggled(const platform::WifiNetwork& network);
  void onConnectionCompleted(int status, const QString& profileName,
                             const platform::WifiNetwork& network);
  void onDisconnected(const QString& profileName);
  void showErrorBriefly(const QString& message);
  QString wifiIcon(int strength, bool secured) const;
  void runAndHide(const QString& uri);
  platform::WifiNetwork* cached(const QString& ssid);

  QWidget* anchor_;
  WifiMenuOptions options_;
  BackendFactory backendFactory_;
  std::unique_ptr<platform::WifiBackend> backend_;
  UriOpener opener_;

  QPointer<MenuPopup> popup_;
  QPointer<LoaderLine> progress_;
  QPointer<ClickableLabel> error_;
  QPointer<WifiList> list_;
  QMetaObject::Connection errorConnection_;
  QTimer* listUpdateTimer_;
  std::vector<platform::WifiNetwork> cache_;
  bool connecting_ = false;
};

}  // namespace yasb::widgets
