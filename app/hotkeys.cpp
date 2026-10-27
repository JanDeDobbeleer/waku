#include "app/hotkeys.h"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QMetaObject>
#include <QScreen>
#include <algorithm>
#include <cctype>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

#include "core/widgets/event_bus.h"

namespace yasb::app {
namespace {

constexpr unsigned kAlt = 0x0001;
constexpr unsigned kControl = 0x0002;
constexpr unsigned kShift = 0x0004;
constexpr unsigned kWin = 0x0008;
constexpr unsigned kNoRepeat = 0x4000;
constexpr unsigned kHotkeyMessage = 0x0312;

unsigned keyCode(const std::string& key) {
  static const std::map<std::string, unsigned> special = {
      {"space", 0x20},     {"tab", 0x09},         {"enter", 0x0D},     {"return", 0x0D},
      {"esc", 0x1B},       {"escape", 0x1B},      {"backspace", 0x08}, {"delete", 0x2E},
      {"del", 0x2E},       {"insert", 0x2D},      {"home", 0x24},      {"end", 0x23},
      {"pageup", 0x21},    {"pagedown", 0x22},    {"left", 0x25},      {"up", 0x26},
      {"right", 0x27},     {"down", 0x28},        {"semicolon", 0xBA}, {"equal", 0xBB},
      {"comma", 0xBC},     {"minus", 0xBD},       {"period", 0xBE},    {"slash", 0xBF},
      {"backquote", 0xC0}, {"bracketleft", 0xDB}, {"backslash", 0xDC}, {"bracketright", 0xDD},
      {"quote", 0xDE}};
  if (const auto found = special.find(key); found != special.end()) return found->second;
  if (key.size() == 1 && std::isalnum(static_cast<unsigned char>(key[0]))) {
    return static_cast<unsigned>(std::toupper(static_cast<unsigned char>(key[0])));
  }
  if (key.size() >= 2 && key[0] == 'f') {
    try {
      const unsigned n = static_cast<unsigned>(std::stoul(key.substr(1)));
      if (n >= 1 && n <= 24) return 0x70 + n - 1;
    } catch (const std::exception&) {
    }
  }
  return 0;
}

}  // namespace

std::optional<HotkeyBinding> parseHotkey(std::string keys, std::string widget, std::string action,
                                         std::string screen) {
  HotkeyBinding binding{std::move(keys), std::move(widget), std::move(action), std::move(screen)};
  std::string key;
  std::istringstream input(binding.keys);
  while (std::getline(input, key, '+')) {
    key.erase(
        std::remove_if(key.begin(), key.end(), [](unsigned char ch) { return std::isspace(ch); }),
        key.end());
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char ch) { return std::tolower(ch); });
    if (key.empty()) continue;
    if (key == "win" || key == "windows" || key == "super" || key == "meta") {
      binding.modifiers |= kWin;
    } else if (key == "alt") {
      binding.modifiers |= kAlt;
    } else if (key == "ctrl" || key == "control") {
      binding.modifiers |= kControl;
    } else if (key == "shift") {
      binding.modifiers |= kShift;
    } else {
      if (binding.virtualKey != 0) return std::nullopt;
      binding.virtualKey = keyCode(key);
      if (binding.virtualKey == 0) return std::nullopt;
    }
  }
  if (binding.virtualKey == 0 || binding.widget.empty() || binding.action.empty())
    return std::nullopt;
  return binding;
}

std::string routeHotkeyScreen(const std::string& mode, const QPoint& cursor,
                              const std::vector<ScreenTarget>& screens,
                              const std::string& primaryScreen) {
  if (mode == "cursor") {
    for (const auto& screen : screens) {
      if (screen.geometry.contains(cursor)) return screen.name;
    }
  } else if (mode != "primary" && mode != "active") {
    for (const auto& screen : screens) {
      if (screen.name == mode) return screen.name;
    }
  }
  for (const auto& screen : screens) {
    if (screen.name == primaryScreen) return screen.name;
  }
  return screens.empty() ? std::string() : screens.front().name;
}

std::vector<HotkeyBinding> collectHotkeys(const nlohmann::json& config) {
  std::vector<HotkeyBinding> bindings;
  const auto widgets = config.find("widgets");
  if (widgets == config.end() || !widgets->is_object()) return bindings;
  for (auto it = widgets->begin(); it != widgets->end(); ++it) {
    const auto options = it.value().find("options");
    if (options == it.value().end() || !options->is_object()) continue;
    const auto list = options->find("keybindings");
    if (list == options->end() || !list->is_array()) continue;
    for (const auto& item : *list) {
      if (!item.is_object()) continue;
      const std::string keys = item.value("keys", std::string());
      const std::string action = item.value("action", std::string());
      const std::string screen = item.value("screen", std::string("active"));
      if (auto parsed = parseHotkey(keys, it.key(), action, screen))
        bindings.push_back(std::move(*parsed));
    }
  }
  return bindings;
}

HotkeyService::HotkeyService(std::vector<HotkeyBinding> bindings)
    : bindings_(std::move(bindings)) {}
HotkeyService::~HotkeyService() { stop(); }

void HotkeyService::start() {
  if (thread_.joinable() || bindings_.empty()) return;
  thread_ = std::thread([this] { run(); });
  while (!ready_.load()) Sleep(1);
}

void HotkeyService::stop() {
  const DWORD id = threadId_.load();
  if (id != 0) PostThreadMessageW(id, WM_QUIT, 0, 0);
  if (thread_.joinable()) thread_.join();
}

void HotkeyService::run() {
  const DWORD id = GetCurrentThreadId();
  MSG message{};
  PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
  threadId_.store(id);
  ready_.store(true);
  std::map<int, HotkeyBinding> registered;
  for (std::size_t i = 0; i < bindings_.size(); ++i) {
    const int hotkeyId = static_cast<int>(i + 1);
    const auto& binding = bindings_[i];
    if (RegisterHotKey(nullptr, hotkeyId, binding.modifiers | kNoRepeat, binding.virtualKey)) {
      registered.emplace(hotkeyId, binding);
      qInfo("Registered hotkey %s", binding.keys.c_str());
    } else {
      qWarning("Could not register hotkey %s", binding.keys.c_str());
    }
  }
  while (!registered.empty()) {
    const BOOL result = GetMessageW(&message, nullptr, 0, 0);
    if (result <= 0) break;
    if (message.message != kHotkeyMessage) continue;
    const auto found = registered.find(static_cast<int>(message.wParam));
    if (found == registered.end()) continue;
    const HotkeyBinding binding = found->second;
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [binding] {
          std::vector<ScreenTarget> screens;
          for (QScreen* screen : QGuiApplication::screens()) {
            screens.push_back({screen->name().toStdString(), screen->geometry()});
          }
          const QString primary = QGuiApplication::primaryScreen()
                                      ? QGuiApplication::primaryScreen()->name()
                                      : QString();
          const std::string target =
              routeHotkeyScreen(binding.screen, QCursor::pos(), screens, primary.toStdString());
          emit widgets::EventBus::instance().widgetHotkey(QString::fromStdString(binding.widget),
                                                          QString::fromStdString(binding.action),
                                                          QString::fromStdString(target));
        },
        Qt::QueuedConnection);
  }
  for (const auto& [hotkeyId, binding] : registered) {
    Q_UNUSED(binding);
    UnregisterHotKey(nullptr, hotkeyId);
  }
  threadId_.store(0);
}

}  // namespace yasb::app
