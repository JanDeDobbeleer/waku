#pragma once

#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include <map>
#include <memory>

#include "core/config/options/bluetooth.h"
#include "platform/bluetooth.h"
#include "widgets/menu_support/loader_line.h"
#include "widgets/menu_support/menu_popup.h"

namespace yasb::widgets {

using BtLabels = std::map<QString, QString>;

// bluetooth_widgets.BluetoothItem: one device row with an expandable action area.
class BluetoothItem : public QFrame {
  Q_OBJECT
 public:
  explicit BluetoothItem(QWidget* parent = nullptr);

  const platform::BluetoothDevice& data() const { return data_; }
  void setData(const platform::BluetoothDevice& data);
  void setLabels(const BtLabels& labels);
  bool active() const { return active_; }
  bool busy() const { return busy_; }
  void setBusy(bool busy, bool disconnecting = false);
  void setActive(bool active);

  QLabel* nameLabel() const { return name_; }
  QLabel* iconLabel() const { return icon_; }
  QLabel* statusLabel() const { return status_; }
  QLabel* batteryLabel() const { return batteryLabel_; }
  QLabel* batteryIcon() const { return batteryIcon_; }
  QPushButton* connectButton() const { return connect_; }
  QFrame* actions() const { return actions_; }

 signals:
  void clicked();
  void connectPressed(const yasb::platform::BluetoothDevice& device);
  void disconnectPressed(const yasb::platform::BluetoothDevice& device);
  void settingsPressed(const yasb::platform::BluetoothDevice& device);

 protected:
  void mousePressEvent(QMouseEvent* event) override;

 private:
  QString actionText() const;
  void onActionClicked();

  bool active_ = false;
  bool busy_ = false;
  platform::BluetoothDevice data_;
  BtLabels labels_;
  QLabel* icon_;
  QLabel* name_;
  QLabel* status_;
  QLabel* batteryLabel_;
  QLabel* batteryIcon_;
  QFrame* actions_;
  QPushButton* connect_;
};

// bluetooth_widgets.BluetoothSection: titled group of items keyed by address.
class BluetoothSection : public QFrame {
  Q_OBJECT
 public:
  BluetoothSection(const QString& title, QWidget* parent = nullptr);

  const QHash<QString, BluetoothItem*>& items() const { return items_; }
  void sync(const std::map<QString, platform::BluetoothDevice>& devices,
            const config::options::BluetoothBatteryIconsConfig& batteryIcons,
            const BtLabels& labels);
  void setActive(const QString& address);

 signals:
  void itemClicked(BluetoothItem* item);
  void connectPressed(const yasb::platform::BluetoothDevice& device);
  void disconnectPressed(const yasb::platform::BluetoothDevice& device);
  void settingsPressed(const yasb::platform::BluetoothDevice& device);

 private:
  QVBoxLayout* layout_;
  QHash<QString, BluetoothItem*> items_;
};

// bluetooth_widgets.BluetoothList: scroll area with the paired and discovered sections.
class BluetoothList : public QScrollArea {
  Q_OBJECT
 public:
  BluetoothList(const QString& pairedTitle, const QString& newTitle, QWidget* parent = nullptr);

  BluetoothSection* paired() const { return paired_; }
  BluetoothSection* discovered() const { return discovered_; }
  BluetoothItem* findItem(const QString& address) const;
  void clearActive();
  void clearBusy();

 signals:
  void connectPressed(const yasb::platform::BluetoothDevice& device);
  void disconnectPressed(const yasb::platform::BluetoothDevice& device);
  void settingsPressed(const yasb::platform::BluetoothDevice& device);

 private:
  void toggleItem(BluetoothItem* item);

  BluetoothSection* paired_;
  BluetoothSection* discovered_;
};

// bluetooth_widgets.BluetoothMenu: the popup UI. Status, scan and connect events are forwarded
// from BluetoothWidget.
class BluetoothMenu : public QObject {
  Q_OBJECT
 public:
  using UriOpener = std::function<void(const QString&)>;

  BluetoothMenu(QWidget* anchor, config::options::BluetoothMenuConfig config,
                std::shared_ptr<platform::BluetoothBackend> backend);

  void detach() { detached_ = true; }
  void showMenu();
  void setUriOpener(UriOpener opener) { opener_ = std::move(opener); }

  void applyStatus(const platform::BluetoothStatus& status);
  void onRefreshFailed(const QString& message);
  void onScanStarted();
  void onScanCompleted(int status, const QVector<platform::BluetoothDevice>& devices);
  void onConnectionFinished(bool success, const QString& message,
                            const platform::BluetoothDevice& device);

  MenuPopup* popup() const { return popup_; }
  BluetoothList* list() const { return list_; }
  QPushButton* powerButton() const { return power_; }
  QLabel* errorLabel() const { return error_; }
  LoaderLine* progress() const { return progress_; }
  QPushButton* scanButton() const { return scan_; }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void build();
  void open();
  void refresh();
  void scan();
  void startLoader();
  void stopLoader();
  void connectDevice(const platform::BluetoothDevice& device);
  void disconnectDevice(const platform::BluetoothDevice& device);
  void toggleRadio(bool checked);
  platform::BluetoothDevice decorate(const platform::BluetoothDevice& device) const;
  void applyList();
  QString powerLabel(bool radioOn) const;
  void setRadioUi(bool radioOn);
  void showError(const QString& message);
  void openUri(const QString& uri);
  BtLabels actionLabels() const;

  QWidget* anchor_;
  config::options::BluetoothMenuConfig config_;
  std::shared_ptr<platform::BluetoothBackend> backend_;
  UriOpener opener_;
  QPointer<MenuPopup> popup_;
  QPointer<BluetoothList> list_;
  QPointer<LoaderLine> progress_;
  QPointer<QPushButton> power_;
  QPointer<QPushButton> scan_;
  QPointer<QLabel> error_;
  std::map<QString, platform::BluetoothDevice> paired_;
  std::map<QString, platform::BluetoothDevice> discovered_;
  bool inquiry_ = false;
  bool scanning_ = false;
  bool detached_ = false;
};

}  // namespace yasb::widgets
