// Fakes of the taskbar's two OS-facing interfaces: windows are rows in a map, shell operations
// only record what they were asked, and nothing touches the desktop, the start menu, the registry
// or the recycle bin. Used by the tracker and pin tests and by the Taskbar service and widget
// tests.
#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <atomic>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "features/taskbar/pin_support.h"
#include "features/taskbar/window_system.h"

namespace wabar::taskbar {

// A window system that never touches the desktop: windows are rows in a map and every action is
// only recorded.
class FakeSystem final : public WindowSystem {
 public:
  struct Row {
    WindowInfo info;
    bool taskbar = true;
    bool iconic = false;
    QString uniqueId = QStringLiteral("app");
    QRect rect{0, 0, 800, 600};
  };

  explicit FakeSystem(QString defaultProcess = QStringLiteral("app.exe"))
      : defaultProcess_(std::move(defaultProcess)) {}

  Row& add(WindowHandle hwnd, const QString& title = QStringLiteral("Explorer"),
           const QString& uniqueId = {}, const QString& process = {}) {
    Row& row = rows[hwnd];
    row.info.hwnd = hwnd;
    row.info.title = title;
    row.info.className = QStringLiteral("TestClass");
    row.info.processName = process;
    row.info.processPath = QStringLiteral("C:\\test\\") + process;
    row.info.pid = 1;
    row.info.canMinimize = true;
    row.info.monitor = widgetMonitor;
    if (!uniqueId.isEmpty()) row.uniqueId = uniqueId;
    return row;
  }

  bool isWindow(WindowHandle hwnd) const override { return rows.count(hwnd) != 0; }
  bool isIconic(WindowHandle hwnd) const override {
    const auto found = rows.find(hwnd);
    return found != rows.end() && found->second.iconic;
  }
  WindowHandle foregroundWindow() const override { return foreground; }
  std::vector<WindowHandle> enumerateWindows() const override {
    std::vector<WindowHandle> result;
    for (const auto& [hwnd, row] : rows) result.push_back(hwnd);
    return result;
  }
  WindowInfo query(WindowHandle hwnd) const override {
    const auto found = rows.find(hwnd);
    return found != rows.end() ? found->second.info : WindowInfo{};
  }
  bool isTaskbarWindow(const WindowInfo& info) const override {
    const auto found = rows.find(info.hwnd);
    return found != rows.end() && found->second.taskbar && !info.title.trimmed().isEmpty();
  }
  bool canMinimize(WindowHandle hwnd) const override { return query(hwnd).canMinimize; }
  WindowHandle monitorOfWindow(WindowHandle) const override { return widgetMonitor; }
  QRect windowRect(WindowHandle hwnd) const override {
    const auto found = rows.find(hwnd);
    return found != rows.end() ? found->second.rect : QRect();
  }
  std::pair<WindowHandle, WindowHandle> resolveBaseAndFocus(WindowHandle hwnd) const override {
    return {hwnd, hwnd};
  }
  bool isOwnerRootActive(WindowHandle base) const override { return foreground == base; }
  std::optional<QString> appUniqueId(const WindowInfo& info) const override {
    const auto found = rows.find(info.hwnd);
    if (found == rows.end() || found->second.uniqueId.isEmpty()) return std::nullopt;
    return found->second.uniqueId;
  }
  IconBitmap windowIcon(WindowHandle, int size) const override {
    IconBitmap bitmap;
    bitmap.width = bitmap.height = size;
    bitmap.bgra = QByteArray(size * size * 4, '\x80');
    return bitmap;
  }

  void restoreWindow(WindowHandle hwnd) override { log << QStringLiteral("restore %1").arg(hwnd); }
  void minimizeWindow(WindowHandle hwnd) override {
    log << QStringLiteral("minimize %1").arg(hwnd);
  }
  void showWindow(WindowHandle hwnd) override { log << QStringLiteral("show %1").arg(hwnd); }
  void setForeground(WindowHandle hwnd) override {
    log << QStringLiteral("foreground %1").arg(hwnd);
  }
  void closeApplication(WindowHandle hwnd, bool force) override {
    log << QStringLiteral("%1 %2").arg(force ? "kill" : "close").arg(hwnd);
  }

  std::map<WindowHandle, Row> rows;
  WindowHandle foreground = 0;
  WindowHandle widgetMonitor = 1;
  QStringList log;

 private:
  QString defaultProcess_;
};

// Shell operations that never touch the start menu, the registry or the recycle bin.
class FakePins final : public PinSupport {
 public:
  PinMetadata resolveMetadata(const WindowInfo& info, const QString&) const override {
    return {info.title, QStringLiteral("C:\\lnk\\") + info.title + QStringLiteral(".lnk")};
  }
  IconBitmap pinnedIcon(const QString& uniqueId, const PinMetadata&, int size) const override {
    iconLoads << uniqueId;
    if (noShellIcon) return {};
    return filled(size, '\x30');
  }
  std::optional<QString> toastIconPath(const QString&) const override { return toastPath; }
  IconBitmap defaultIcon() const override { return filled(16, '\x77'); }
  IconBitmap recycleBinIcon(bool empty, int size) const override {
    return filled(size, empty ? '\x40' : '\xC0');
  }
  void launch(const QString& uniqueId, const PinMetadata& metadata, const QString& extra) override {
    launchedIds << uniqueId;
    launches << uniqueId + QLatin1Char('|') + extra + QLatin1Char('|') + metadata.shortcutPath;
  }
  RecycleBinInfo recycleBinInfo() const override { return bin; }
  void emptyRecycleBin() override { ++emptied; }
  std::vector<QuickAccessFolder> quickAccessFolders() const override { return folders; }

  static IconBitmap filled(int size, char value) {
    IconBitmap bitmap;
    bitmap.width = bitmap.height = size;
    bitmap.bgra = QByteArray(size * size * 4, value);
    return bitmap;
  }

  mutable QStringList iconLoads;
  QStringList launches;     // "<id>|<extra>|<shortcut>" per launch
  QStringList launchedIds;  // the ids alone
  bool noShellIcon = false;
  std::optional<QString> toastPath;
  RecycleBinInfo bin;
  std::atomic<int> emptied = 0;
  std::vector<QuickAccessFolder> folders;
};

}  // namespace wabar::taskbar
