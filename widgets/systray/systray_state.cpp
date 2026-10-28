#include "widgets/systray/systray_state.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace yasb::widgets {
namespace {

using platform::TrayIconData;

bool truthy(const nlohmann::json& value) {
  if (value.is_boolean()) return value.get<bool>();
  if (value.is_number_integer()) return value.get<long long>() != 0;
  if (value.is_number_float()) return value.get<double>() != 0.0;
  if (value.is_string()) return !value.get<std::string>().empty();
  if (value.is_null()) return false;
  return !value.empty();
}

// int(str(value)) for the shapes JSON can hold; nullopt where Python would raise.
std::optional<int> toIndex(const nlohmann::json& value) {
  if (value.is_number_integer()) return static_cast<int>(value.get<long long>());
  if (value.is_boolean()) return value.get<bool>() ? 1 : 0;
  if (value.is_string()) {
    bool ok = false;
    const int parsed = QString::fromStdString(value.get<std::string>()).trimmed().toInt(&ok);
    if (ok) return parsed;
  }
  return std::nullopt;
}

std::optional<nlohmann::ordered_json> readJsonFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
  const QByteArray bytes = file.readAll();
  auto parsed = nlohmann::ordered_json::parse(bytes.constData(), bytes.constData() + bytes.size(),
                                              nullptr, false);
  if (parsed.is_discarded()) return std::nullopt;
  return parsed;
}

}  // namespace

const IconState* IconStateMap::find(const QString& key) const {
  for (const Entry& entry : entries_) {
    if (entry.first == key) return &entry.second;
  }
  return nullptr;
}

void IconStateMap::set(const QString& key, const IconState& state) {
  for (Entry& entry : entries_) {
    if (entry.first == key) {
      entry.second = state;
      return;
    }
  }
  entries_.emplace_back(key, state);
}

QString iconStateKey(const TrayIconData& data) {
  return data.guid ? data.guid->toString(QUuid::WithoutBraces) : data.exePath;
}

void updateCurrentState(IconStateMap& state, const QStringList& pinned,
                        const QStringList& unpinned) {
  for (const bool isPinned : {true, false}) {
    const QStringList& keys = isPinned ? pinned : unpinned;
    const QStringList& movedOut = isPinned ? unpinned : pinned;

    std::vector<IconStateMap::Entry> saved;
    for (const auto& entry : state.entries()) {
      if (entry.second.isPinned == isPinned && !movedOut.contains(entry.first))
        saved.push_back(entry);
    }
    std::stable_sort(saved.begin(), saved.end(),
                     [](const auto& a, const auto& b) { return a.second.index < b.second.index; });

    QStringList order;
    qsizetype next = 0;
    for (const auto& entry : saved) {
      if (keys.contains(entry.first) && next < keys.size())
        order.append(keys.at(next++));
      else
        order.append(entry.first);
    }
    while (next < keys.size()) order.append(keys.at(next++));

    int index = 0;
    for (const QString& key : order) state.set(key, IconState{isPinned, index++});
  }
}

void updateIconData(TrayIconData& old, const TrayIconData& incoming) {
  old.messageType = incoming.messageType;
  old.uFlags = incoming.uFlags;
  old.iconWidth = incoming.iconWidth;
  old.iconHeight = incoming.iconHeight;
  old.iconRgba = incoming.iconRgba;
  old.exe = incoming.exe;
  old.exePath = incoming.exePath;
  old.hWnd = incoming.hWnd ? incoming.hWnd : old.hWnd;
  old.uID = incoming.uID ? incoming.uID : old.uID;
  if (incoming.uVersion > 0 && incoming.uVersion <= 4) old.uVersion = incoming.uVersion;

  const std::uint32_t flags = incoming.uFlags;
  if (flags & platform::kNifMessage) old.uCallbackMessage = incoming.uCallbackMessage;
  if (flags & platform::kNifIcon) old.hIcon = incoming.hIcon;
  if (flags & platform::kNifTip) old.szTip = incoming.szTip;
  if (flags & platform::kNifState) {
    old.dwState = incoming.dwState;
    old.dwStateMask = incoming.dwStateMask;
  }
  if (flags & platform::kNifGuid) old.guid = incoming.guid;
  if (flags & platform::kNifInfo) {
    old.dwInfoFlags = incoming.dwInfoFlags;
    old.szInfoTitle = incoming.szInfoTitle;
    old.szInfo = incoming.szInfo;
    old.uTimeout = incoming.uTimeout;
  }
}

IconState savedStateFor(const IconStateMap& state, const platform::TrayIconData& data) {
  const IconState* saved = state.find(iconStateKey(data));
  if (!saved) saved = state.find(data.exePath);
  return saved ? *saved : IconState{false, -1};
}

QString appDataDir(const QString& folder) {
  QString base = qEnvironmentVariable("LOCALAPPDATA");
  if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/AppData/Local");
  const QString dir = QDir(base).filePath(folder);
  QDir().mkpath(dir);
  return dir;
}

QString SystrayStateStore::path() const {
  return QDir(dir_).filePath(QStringLiteral("systray_state_%1.json").arg(screenId_));
}

IconStateMap SystrayStateStore::load() const {
  IconStateMap state;
  const auto document = readJsonFile(path());
  if (!document || !document->is_object()) return state;
  for (const auto& [key, value] : document->items()) {
    if (!value.is_object()) continue;
    const auto pinned = value.find("is_pinned");
    const auto index = value.find("index");
    if (pinned == value.end() || index == value.end()) continue;
    const auto parsedIndex = toIndex(*index);
    if (!parsedIndex) continue;
    state.set(QString::fromStdString(key), IconState{truthy(*pinned), *parsedIndex});
  }
  return state;
}

bool SystrayStateStore::save(const IconStateMap& state) const {
  nlohmann::ordered_json document = nlohmann::ordered_json::object();
  if (auto existing = readJsonFile(path()); existing && existing->is_object())
    document = std::move(*existing);
  for (const auto& [key, value] : state.entries()) {
    document[key.toStdString()] =
        nlohmann::ordered_json{{"is_pinned", value.isPinned}, {"index", value.index}};
  }
  // json.dumps(indent=2) written by Python in text mode: ASCII-only, CRLF line endings.
  QByteArray text = QByteArray::fromStdString(document.dump(2, ' ', true));
  text.replace("\n", "\r\n");
  QDir().mkpath(dir_);
  QSaveFile file(path());
  if (!file.open(QIODevice::WriteOnly)) return false;
  file.write(text);
  return file.commit();
}

QString cleanScreenId(const QString& manufacturer, const QString& name, const QString& serial) {
  static const QRegularExpression nonWord(QStringLiteral("\\W+"),
                                          QRegularExpression::UseUnicodePropertiesOption);
  QString id = (manufacturer + name + serial).toUpper();
  id.remove(nonWord);
  return id;
}

}  // namespace yasb::widgets
