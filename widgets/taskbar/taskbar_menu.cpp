#include "widgets/taskbar/taskbar_menu.h"

#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPointer>
#include <QRunnable>
#include <QScreen>
#include <QStyle>
#include <QStyleFactory>
#include <QThreadPool>
#include <exception>
#include <functional>

#include "core/bar/bar_window.h"
#include "core/widgets/backdrop.h"
#include "platform/pin_support.h"
#include "widgets/taskbar/recycle_bin_monitor.h"
#include "widgets/taskbar/taskbar_widget.h"

namespace yasb::widgets {

std::vector<TerminalProfile> windowsTerminalProfiles(const QString& identifier,
                                                     const QString& localAppData) {
  const QString lowered = identifier.toLower();
  const bool portable = identifier.startsWith(QLatin1String("path:")) &&
                        lowered.contains(QLatin1String("windowsterminal.exe"));
  const bool preview = lowered.contains(QLatin1String("windowsterminalpre"));

  QString settings;
  if (portable) {
    // A portable install keeps its settings in %LOCALAPPDATA%\Microsoft\Windows Terminal.
    settings = localAppData + QStringLiteral("/Microsoft/Windows Terminal/settings.json");
  } else {
    const QString package = preview
                                ? QStringLiteral("Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe")
                                : QStringLiteral("Microsoft.WindowsTerminal_8wekyb3d8bbwe");
    settings = localAppData + QStringLiteral("/Packages/") + package +
               QStringLiteral("/LocalState/settings.json");
  }

  QFile file(settings);
  if (!file.open(QIODevice::ReadOnly)) return {};
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) return {};
  const QJsonValue list =
      document.object().value(QStringLiteral("profiles")).toObject().value(QStringLiteral("list"));

  std::vector<TerminalProfile> profiles;
  for (const QJsonValue& entry : list.toArray()) {
    const QJsonObject profile = entry.toObject();
    // Hidden profiles are skipped.
    if (profile.value(QStringLiteral("hidden")).toBool(false)) continue;
    const QString guid = profile.value(QStringLiteral("guid")).toString();
    if (guid.isEmpty()) continue;
    profiles.push_back(
        {profile.value(QStringLiteral("name")).toString(QStringLiteral("Unknown")), guid});
    if (profiles.size() >= 20) break;
  }
  return profiles;
}

QPoint contextMenuPosition(const QRect& barGeometry, const QPoint& buttonCenter,
                           const QSize& menuSize, bool barOnTop) {
  const int margin = 6;
  const int x = buttonCenter.x() - menuSize.width() / 2;
  const int y = barOnTop ? barGeometry.top() + barGeometry.height() + margin
                         : barGeometry.top() - menuSize.height() - margin;
  return QPoint(x, y);
}

QString naturalSizeBinary(qint64 bytes) {
  if (bytes == 1) return QStringLiteral("1 Byte");
  if (bytes < 1024) return QStringLiteral("%1 Bytes").arg(bytes);
  static const char* const suffixes[] = {"KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
  double unit = 1024.0 * 1024.0;
  for (const char* suffix : suffixes) {
    if (static_cast<double>(bytes) < unit)
      return QString::number(1024.0 * static_cast<double>(bytes) / unit, 'f', 2) +
             QLatin1Char(' ') + QLatin1String(suffix);
    unit *= 1024.0;
  }
  return QString::number(static_cast<double>(bytes), 'f', 2) + QStringLiteral(" Bytes");
}

namespace {

using LaunchFn = std::function<void(const QString& target, const QString& extraArguments)>;

void addLaunchAction(QMenu* menu, const QString& text, const LaunchFn& launch,
                     const QString& target, const QString& extraArguments = QString()) {
  auto* action = new QAction(text, menu);
  menu->addAction(action);
  QObject::connect(action, &QAction::triggered, menu,
                   [launch, target, extraArguments] { launch(target, extraArguments); });
}

}  // namespace

// Context menu of a taskbar button (app_menu.show_context_menu): app specific entries, Open app,
// Pin/Unpin, End task, Close window and Close all windows.
QMenu* TaskbarWidget::showContextMenu(platform::WindowHandle hwnd, const QPoint& pos) {
  contextMenuOpen_ = false;
  QMenu* menu = nullptr;
  try {
    // Remove the cursor override while the menu is open; it is restored when the menu hides.
    QPointer<AppButton> widget = buttonFor(hwnd);
    if (widget) widget->unsetCursor();

    const bool pinnedOnly = isPseudoHwnd(hwnd);
    const bool pinned = pinnedOnly || isAppPinned(hwnd);

    QString uniqueId;
    if (pinnedOnly) {
      if (widget) uniqueId = widget->property("unique_id").toString();
    } else if (pinned) {
      uniqueId = runningPinned_.at(hwnd);
    } else {
      // Running but not pinned: the real id of the window.
      platform::WindowInfo info;
      const auto entry = windowButtons_.find(hwnd);
      info = entry != windowButtons_.end() ? entry->second.info : system_->query(hwnd);
      if (const auto id = system_->appUniqueId(info)) uniqueId = *id;
    }

    menu = new QMenu(window());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    // apply_qmenu_style: Fusion removes the menu shadow, blur and round corners on show.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
      fusion->setParent(menu);
      menu->setStyle(fusion);
    }
    connect(menu, &QMenu::aboutToShow, menu, [menu] {
      try {
        enableBlur(menu->winId(), false, true, QStringLiteral("normal"), QStringLiteral("None"));
      } catch (...) {
      }
    });
    menu->setProperty("class", "context-menu");

    const LaunchFn launch = [this](const QString& target, const QString& extra) {
      launchPinnedApp(target, extra);
    };

    // The app type comes from the id.
    bool isExplorer = false;
    bool isRecycleBin = false;
    bool isChromium = false;
    bool isFirefox = false;
    bool isVsCode = false;
    bool isTerminal = false;
    if (!uniqueId.isEmpty()) {
      const QString check = uniqueId.toLower();
      isExplorer = check.contains(QLatin1String("explorer.exe")) ||
                   check.startsWith(QLatin1String("explorer:"));
      isRecycleBin = platform::isRecycleBinId(uniqueId);
      isChromium =
          check.contains(QLatin1String("msedge")) || check.contains(QLatin1String("chrome"));
      isFirefox = check.contains(QLatin1String("firefox.exe")) ||
                  check.contains(QLatin1String("308046b0af4a39cb")) ||
                  check.contains(QLatin1String("zen.exe")) ||
                  check.contains(QLatin1String("f0dc299d809b9700"));
      isVsCode = check.contains(QLatin1String("code.exe")) ||
                 check.contains(QLatin1String("code - insiders.exe"));
      isTerminal = check.contains(QLatin1String("windowsterminal"));
    }

    if (isRecycleBin && pinned) {
      // Item count and size from the monitor's last answer (fast, non-blocking).
      const platform::RecycleBinInfo info =
          recycleBin_ ? recycleBin_->last() : platform::RecycleBinInfo{};
      if (info.numItems > 0) {
        auto* summary =
            menu->addAction(QStringLiteral("%1 item%2 (%3)")
                                .arg(info.numItems)
                                .arg(info.numItems != 1 ? QStringLiteral("s") : QString())
                                .arg(naturalSizeBinary(info.sizeBytes)));
        summary->setEnabled(false);
        menu->addSeparator();
      }
      addLaunchAction(menu, QStringLiteral("Open"), launch,
                      QStringLiteral("explorer:::{645FF040-5081-101B-9F08-00AA002F954E}"));
      menu->addSeparator();
      QAction* empty = menu->addAction(QStringLiteral("Empty Recycle Bin"));
      const bool isEmpty = info.numItems == 0 && info.sizeBytes == 0;
      empty->setEnabled(!isEmpty);
      if (isEmpty) empty->setToolTip(QStringLiteral("Recycle Bin is already empty"));
      connect(empty, &QAction::triggered, this, [pins = pins_] {
        // The shell asks for confirmation and shows progress, so it runs off the GUI thread.
        QThreadPool::globalInstance()->start(QRunnable::create([pins] {
          try {
            pins->emptyRecycleBin();
          } catch (...) {
          }
        }));
      });
      menu->addSeparator();
    } else if (isExplorer) {
      // Folders pinned to Quick Access, then File Explorer itself (This PC).
      const auto folders = pins_->quickAccessFolders();
      for (const platform::QuickAccessFolder& folder : folders)
        addLaunchAction(menu, folder.name, launch, QStringLiteral("explorer:") + folder.path);
      if (!folders.empty()) menu->addSeparator();
      addLaunchAction(menu, QStringLiteral("File Explorer"), launch,
                      QStringLiteral("explorer:::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"));
      menu->addSeparator();
    } else if (isChromium && !uniqueId.isEmpty()) {
      const bool isChrome = uniqueId.toLower().contains(QLatin1String("chrome"));
      addLaunchAction(menu, QStringLiteral("New Window"), launch, uniqueId);
      addLaunchAction(menu,
                      isChrome ? QStringLiteral("New Incognito Window")
                               : QStringLiteral("New InPrivate Window"),
                      launch, uniqueId,
                      isChrome ? QStringLiteral("--incognito") : QStringLiteral("--inprivate"));
      menu->addSeparator();
    } else if (isFirefox && !uniqueId.isEmpty()) {
      if (uniqueId.startsWith(QLatin1String("aumid:"))) {
        // A Store build takes the private browsing AUMID.
        addLaunchAction(menu, QStringLiteral("New Window"), launch, uniqueId);
        addLaunchAction(menu, QStringLiteral("New Private Window"), launch,
                        uniqueId + QStringLiteral(";PrivateBrowsingAUMID"));
      } else {
        addLaunchAction(menu, QStringLiteral("New Window"), launch, uniqueId,
                        QStringLiteral("-new-window"));
        addLaunchAction(menu, QStringLiteral("New Private Window"), launch, uniqueId,
                        QStringLiteral("-private-window"));
      }
      menu->addSeparator();
    } else if (isVsCode && !uniqueId.isEmpty()) {
      addLaunchAction(menu, QStringLiteral("New Window"), launch, uniqueId, QStringLiteral("-n"));
      menu->addSeparator();
    } else if (isTerminal && !uniqueId.isEmpty()) {
      const auto profiles = windowsTerminalProfiles(
          uniqueId, qEnvironmentVariable("LOCALAPPDATA").replace('\\', '/'));
      for (const TerminalProfile& profile : profiles)
        addLaunchAction(menu, profile.name, launch, uniqueId, QStringLiteral("-p ") + profile.guid);
      if (!profiles.empty()) menu->addSeparator();
      addLaunchAction(menu, QStringLiteral("New Window"), launch, uniqueId);
      menu->addSeparator();
    }

    menu->addSeparator();

    // Open app: pinned apps only (not running apps that are not pinned), never for Explorer.
    if (pinned && !isExplorer) {
      connect(menu->addAction(QStringLiteral("Open app")), &QAction::triggered, this,
              [this, hwnd] { launchPinnedApp(hwnd); });
    }

    // Pin or unpin.
    if (pinned) {
      menu->addSeparator();
      QAction* unpin = menu->addAction(QStringLiteral("Unpin from taskbar"));
      if (pinnedOnly)
        connect(unpin, &QAction::triggered, this,
                [this, hwnd, uniqueId] { unpinPinnedOnlyApp(uniqueId, hwnd); });
      else
        connect(unpin, &QAction::triggered, this, [this, hwnd] { unpinApp(hwnd); });
    } else {
      QAction* pin = menu->addAction(QStringLiteral("Pin to taskbar"));
      connect(pin, &QAction::triggered, this, [this, hwnd] { pinApp(hwnd); });
      menu->addSeparator();
    }

    // End task and Close window apply to running apps only.
    if (!pinnedOnly && system_->isWindow(hwnd)) {
      connect(menu->addAction(QStringLiteral("End task")), &QAction::triggered, this,
              [this, hwnd] { system_->closeApplication(hwnd, true); });
      connect(menu->addAction(QStringLiteral("Close window")), &QAction::triggered, this,
              [this, hwnd] { system_->closeApplication(hwnd, false); });
      const std::vector<platform::WindowHandle> members = membersOf(hwnd);
      if (members.size() > 1) {
        connect(menu->addAction(QStringLiteral("Close all windows")), &QAction::triggered, this,
                [this, members] {
                  for (platform::WindowHandle member : members)
                    system_->closeApplication(member, false);
                });
      }
    }

    // Place the menu just outside the bar, centred on the button: below a top bar, above a bottom
    // bar (the bar's configured position, not the half of the screen it happens to be in).
    QWidget* bar = window();
    const QPoint center = widget ? widget->mapToGlobal(widget->rect().center()) : pos;
    bool barOnTop;
    if (const auto* barWindow = qobject_cast<const bar::BarWindow*>(bar)) {
      barOnTop = barWindow->settings().position == "top";
    } else {
      const QScreen* screen = QApplication::screenAt(center);
      barOnTop =
          screen && bar && bar->frameGeometry().center().y() < screen->geometry().center().y();
    }
    const QPoint position =
        contextMenuPosition(bar ? QRect(bar->mapToGlobal(QPoint(0, 0)), bar->size()) : QRect(),
                            center, menu->sizeHint(), barOnTop);
    // popup() rather than exec() lets focus be handled properly.
    menu->popup(position);
    menu->activateWindow();

    contextMenuOpen_ = true;
    connect(menu, &QMenu::aboutToHide, this, [this, hwnd, widget] {
      contextMenuOpen_ = false;
      if (widget) widget->setCursor(Qt::PointingHandCursor);
      if (options_.title_label.enabled && options_.title_label.show == "focused") {
        for (const auto& [key, button] :
             std::map<platform::WindowHandle, QPointer<AppButton>>(hwndToWidget_)) {
          if (!isPseudoHwnd(key)) refreshTitleVisibility(key);
        }
      } else {
        refreshTitleVisibility(hwnd);
      }
    });
    return menu;
  } catch (const std::exception& error) {
    qWarning().noquote() << "Taskbar context menu failed:" << error.what();
    delete menu;
    return nullptr;
  }
}

}  // namespace yasb::widgets
