#include "platform/pin_support.h"

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
// clang-format on

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <map>
#include <tuple>
#include <utility>

#include "platform/app_identity.h"

namespace yasb::platform {
namespace {

const PROPERTYKEY kAppUserModelId = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

constexpr const char* kRecycleBinClsid = "645FF040-5081-101B-9F08-00AA002F954E";
constexpr const char* kQuickAccessClsid = "679F85CB-0220-4080-B29B-5540CC05AAB6";

const wchar_t* wide(const QString& text) { return reinterpret_cast<const wchar_t*>(text.utf16()); }

QString systemRoot() {
  QString root = qEnvironmentVariable("SystemRoot");
  if (root.isEmpty()) root = qEnvironmentVariable("WINDIR");
  return root.isEmpty() ? QStringLiteral("C:\\Windows") : root;
}

QString expandEnvironment(const QString& text) {
  if (!text.contains(QLatin1Char('%'))) return text;
  const DWORD needed = ExpandEnvironmentStringsW(wide(text), nullptr, 0);
  if (needed == 0) return text;
  std::vector<wchar_t> buffer(needed);
  if (ExpandEnvironmentStringsW(wide(text), buffer.data(), needed) == 0) return text;
  return QString::fromWCharArray(buffer.data());
}

// Python's canonical_display_key: lower case, dashes and underscores as spaces, runs of white space
// collapsed. Empty when nothing is left.
QString canonicalDisplayKey(const QString& name) {
  QString value = name.trimmed().toLower();
  if (value.isEmpty()) return {};
  value.replace(QLatin1Char('-'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
  static const QRegularExpression spaces(QStringLiteral("\\s+"));
  value.replace(spaces, QStringLiteral(" "));
  return value;
}

std::vector<QString> startMenuRoots() {
  std::vector<QString> roots;
  for (const char* variable : {"APPDATA", "PROGRAMDATA"}) {
    const QString base = qEnvironmentVariable(variable);
    if (base.isEmpty()) continue;
    const QString path = base + QStringLiteral("\\Microsoft\\Windows\\Start Menu\\Programs");
    if (QFileInfo(path).isDir()) roots.push_back(path);
  }
  return roots;
}

std::optional<QString> shortcutAumid(const QString& path) {
  ComScope com;
  IShellLinkW* link = nullptr;
  if (FAILED(
          CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
      !link) {
    return std::nullopt;
  }
  std::optional<QString> result;
  IPersistFile* file = nullptr;
  if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))) && file) {
    if (SUCCEEDED(file->Load(wide(path), STGM_READ))) {
      IPropertyStore* store = nullptr;
      if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&store))) && store) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(kAppUserModelId, &value)) && value.vt == VT_LPWSTR &&
            value.pwszVal && *value.pwszVal) {
          result = QString::fromWCharArray(value.pwszVal);
        }
        PropVariantClear(&value);
        store->Release();
      }
    }
    file->Release();
  }
  link->Release();
  return result;
}

std::optional<QString> appNameFromAumid(const QString& aumid) {
  ComScope com;
  IShellItem* item = nullptr;
  if (FAILED(SHCreateItemFromParsingName(wide(QStringLiteral("shell:AppsFolder\\") + aumid),
                                         nullptr, IID_PPV_ARGS(&item))) ||
      !item) {
    return std::nullopt;
  }
  std::optional<QString> result;
  PWSTR name = nullptr;
  if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name) {
    result = QString::fromWCharArray(name);
    CoTaskMemFree(name);
  }
  item->Release();
  if (result && result->isEmpty()) return std::nullopt;
  return result;
}

bool shellOpen(const QString& target, const QString& arguments = QString(),
               const QString& workingDirectory = QString()) {
  const HINSTANCE result =
      ShellExecuteW(nullptr, L"open", wide(target), arguments.isEmpty() ? nullptr : wide(arguments),
                    workingDirectory.isEmpty() ? nullptr : wide(workingDirectory), SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
}

// An icon of `size` pixels from a file (exe, dll, ico) through PrivateExtractIconsW, which picks
// the closest picture of the resource instead of scaling the 32 pixel one.
IconBitmap extractedIcon(const QString& file, int index, int size) {
  HICON icon = nullptr;
  UINT id = 0;
  if (PrivateExtractIconsW(wide(file), index, size, size, &icon, &id, 1, LR_DEFAULTCOLOR) == 0 ||
      !icon) {
    return {};
  }
  IconBitmap bitmap = iconBitmapFromHicon(icon);
  DestroyIcon(icon);
  return bitmap;
}

class Win32PinSupport final : public PinSupport {
 public:
  PinMetadata resolveMetadata(const WindowInfo& info, const QString& uniqueId) const override {
    const UniqueIdParts parts = parseUniqueId(uniqueId);
    const bool hasAumid = parts.type == QLatin1String("aumid");
    const QString aumid = hasAumid ? parts.value : QString();
    const QString exe = info.processPath;
    const QString process = info.processName.value_or(QString());
    const bool isExplorer = exe.contains(QLatin1String("explorer.exe"), Qt::CaseInsensitive);
    const bool hasExplorerPath = parts.type == QLatin1String("explorer");

    // PinManager._app_display_name
    QString baseTitle;
    if (isExplorer)
      baseTitle = info.title;
    else if (!hasAumid)
      baseTitle = info.title.isEmpty() ? process : info.title;
    if (baseTitle.isEmpty() && hasAumid) baseTitle = appNameFromAumid(aumid).value_or(QString());
    if (baseTitle.isEmpty()) baseTitle = info.title.isEmpty() ? process : info.title;

    QString shortcutPath;
    QString shortcutName;
    QStringList candidates;
    for (const QString& name : {info.title, process, QFileInfo(exe).completeBaseName()}) {
      if (!name.isEmpty()) candidates << name;
    }

    const auto searchByExe = [&] {
      for (const QString& key : normalizedTargets(exe)) {
        const auto cached = shortcutCache_.find(key);
        if (cached == shortcutCache_.end()) continue;
        if (QFileInfo::exists(cached->second)) {
          shortcutPath = cached->second;
          shortcutName = QFileInfo(shortcutPath).completeBaseName();
          return;
        }
        shortcutCache_.erase(cached);
      }
      if (candidates.isEmpty()) return;
      if (const auto found = findAppShortcut(exe, candidates)) {
        std::tie(shortcutPath, shortcutName) = *found;
      } else if (const auto byName = findShortcutByName(candidates)) {
        std::tie(shortcutPath, shortcutName) = *byName;
        for (const QString& key : normalizedTargets(exe)) shortcutCache_[key] = shortcutPath;
      }
    };

    if (!exe.isEmpty() && !hasAumid && !isExplorer && !hasExplorerPath) {
      searchByExe();
    } else if (hasAumid) {
      // AUMID apps are either Store/PWA apps (the id ends in "!App", launched through
      // shell:AppsFolder) or Win32 apps with an AUMID (Steam, Electron), which need a shortcut.
      if (const auto found = findShortcutByAumid(aumid)) {
        std::tie(shortcutPath, shortcutName) = *found;
      } else if (!aumid.endsWith(QLatin1String("!App")) && !exe.isEmpty()) {
        searchByExe();
      }
    }

    PinMetadata metadata;
    QString best = baseTitle;
    if (!shortcutPath.isEmpty()) {
      metadata.shortcutPath = shortcutPath;
      if (shortcutName.isEmpty()) shortcutName = QFileInfo(shortcutPath).completeBaseName();
    }
    // With a shortcut its name is the most reliable title. Without one, apps that are not Store
    // apps prefer the window title, Store apps their name (the window title can be a tab or
    // document).
    if (!shortcutName.isEmpty()) {
      best = shortcutName;
    } else if (!hasAumid || baseTitle.isEmpty()) {
      if (!info.title.isEmpty())
        best = info.title;
      else if (!baseTitle.isEmpty())
        best = baseTitle;
      else if (!process.isEmpty())
        best = process;
    } else {
      best = !baseTitle.isEmpty() ? baseTitle : (!info.title.isEmpty() ? info.title : process);
    }
    if (best.isEmpty()) best = info.title.isEmpty() ? process : info.title;
    metadata.title = best;
    return metadata;
  }

  IconBitmap pinnedIcon(const QString& uniqueId, const PinMetadata& metadata,
                        int size) const override {
    const UniqueIdParts parts = parseUniqueId(uniqueId);
    ComScope com;
    if (parts.type == QLatin1String("aumid"))
      return shellItemIcon(QStringLiteral("shell:AppsFolder\\") + parts.value, size);
    if (!metadata.shortcutPath.isEmpty() && QFileInfo(metadata.shortcutPath).isFile())
      return shortcutIcon(metadata.shortcutPath, size);
    if (parts.type == QLatin1String("explorer")) return shellItemIcon(parts.value, size);
    if (parts.type == QLatin1String("path")) {
      const QString tool =
          toolFileFromArguments(parts.args, systemRoot() + QStringLiteral("\\System32"));
      return shellItemIcon(tool.isEmpty() ? parts.value : tool, size);
    }
    return {};
  }

  std::optional<QString> toastIconPath(const QString& aumid) const override {
    const QString key = QStringLiteral("Software\\Classes\\AppUserModelId\\") + aumid;
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
      DWORD bytes = 0;
      if (RegGetValueW(root, wide(key), L"IconUri", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr,
                       nullptr, &bytes) != ERROR_SUCCESS ||
          bytes < sizeof(wchar_t)) {
        continue;
      }
      std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
      if (RegGetValueW(root, wide(key), L"IconUri", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr,
                       buffer.data(), &bytes) != ERROR_SUCCESS) {
        continue;
      }
      const QString icon = QString::fromWCharArray(buffer.data());
      // ms-resource:// and @{...} need the package resource loader.
      if (icon.isEmpty() || icon.contains(QLatin1String("://")) ||
          icon.startsWith(QLatin1Char('@'))) {
        continue;
      }
      return expandEnvironment(icon);
    }
    return std::nullopt;
  }

  IconBitmap defaultIcon() const override { return defaultAppIcon(); }

  IconBitmap recycleBinIcon(bool empty, int) const override {
    SHSTOCKICONINFO info = {};
    info.cbSize = sizeof(info);
    // SIID_RECYCLER (31) is the empty bin, SIID_RECYCLERFULL (32) the full one.
    if (FAILED(SHGetStockIconInfo(empty ? SIID_RECYCLER : SIID_RECYCLERFULL,
                                  SHGSI_ICON | SHGSI_LARGEICON, &info)) ||
        !info.hIcon) {
      return {};
    }
    IconBitmap bitmap = iconBitmapFromHicon(info.hIcon);
    DestroyIcon(info.hIcon);
    return bitmap;
  }

  void launch(const QString& uniqueId, const PinMetadata& metadata,
              const QString& extraArguments) override {
    const UniqueIdParts parts = parseUniqueId(uniqueId);
    const QString& shortcutPath = metadata.shortcutPath;

    if (parts.type == QLatin1String("aumid") && shortcutPath.isEmpty()) {
      // shell:AppsFolder for plain launches avoids falling back to the default browser.
      const QString folder = QStringLiteral("shell:AppsFolder\\") + parts.value;
      if (extraArguments.isEmpty()) {
        if (!shellOpen(folder)) shellOpen(parts.value);
      } else if (!shellOpen(parts.value, extraArguments) && !shellOpen(folder, extraArguments)) {
        shellOpen(parts.value, extraArguments);
      }
      return;
    }
    if (parts.type == QLatin1String("aumid") && !shortcutPath.isEmpty() &&
        QFileInfo::exists(shortcutPath) && extraArguments.isEmpty()) {
      shellOpen(shortcutPath);
      return;
    }
    // File Explorer folders and special shell locations (C:\folder, shell:RecycleBinFolder,
    // ::{...}).
    if (parts.type == QLatin1String("explorer")) {
      shellOpen(parts.value);
      return;
    }

    QString target;
    QString arguments;
    QString workingDirectory;
    if (!shortcutPath.isEmpty() && QFileInfo::exists(shortcutPath)) {
      if (const auto shortcut = readShortcut(shortcutPath)) {
        target = shortcut->target;
        arguments = shortcut->arguments;
        workingDirectory = shortcut->workingDirectory;
      }
    }
    if (target.isEmpty() && parts.type == QLatin1String("path") && QFileInfo::exists(parts.value))
      target = parts.value;

    // Command line arguments in the id (mmc.exe with a .msc file) win over the shortcut's.
    if (!parts.args.isEmpty()) {
      if (parts.type == QLatin1String("path") && QFileInfo::exists(parts.value))
        target = parts.value;
      if (!arguments.isEmpty() && !parts.args.contains(arguments))
        arguments = (parts.args + QLatin1Char(' ') + arguments).trimmed();
      else
        arguments = parts.args;
    }

    if (target.isEmpty()) {
      qWarning().noquote() << "Taskbar: cannot launch" << uniqueId << "- no valid target";
      return;
    }
    if (workingDirectory.isEmpty() || !QFileInfo::exists(workingDirectory))
      workingDirectory = QFileInfo(target).absolutePath();
    if (!QFileInfo::exists(workingDirectory)) workingDirectory.clear();
    // ShellExecute raises the UAC prompt for apps that require elevation.
    if (!shellOpen(target, (arguments + QLatin1Char(' ') + extraArguments).trimmed(),
                   workingDirectory)) {
      qWarning().noquote() << "Taskbar: failed to launch" << target;
    }
  }

  RecycleBinInfo recycleBinInfo() const override {
    SHQUERYRBINFO info = {};
    info.cbSize = sizeof(info);
    if (FAILED(SHQueryRecycleBinW(nullptr, &info))) return {};
    return {static_cast<qint64>(info.i64Size), static_cast<qint64>(info.i64NumItems)};
  }

  void emptyRecycleBin() override {
    ComScope com;
    // Confirmation and progress UI stay on; only the sound is off (SHERB_NOSOUND).
    const HRESULT result = SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOSOUND);
    if (FAILED(result)) qWarning() << "Taskbar: failed to empty the Recycle Bin" << result;
  }

  std::vector<QuickAccessFolder> quickAccessFolders() const override {
    ComScope com;
    std::vector<QuickAccessFolder> folders;
    IShellItem* quickAccess = nullptr;
    const QString name =
        QStringLiteral("shell:::{") + QLatin1String(kQuickAccessClsid) + QLatin1Char('}');
    if (FAILED(SHCreateItemFromParsingName(wide(name), nullptr, IID_PPV_ARGS(&quickAccess))) ||
        !quickAccess) {
      return folders;
    }
    IEnumShellItems* items = nullptr;
    if (SUCCEEDED(quickAccess->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))) &&
        items) {
      IShellItem* item = nullptr;
      ULONG fetched = 0;
      while (folders.size() < 10 && items->Next(1, &item, &fetched) == S_OK && item) {
        SFGAOF attributes = 0;
        item->GetAttributes(SFGAO_FOLDER | SFGAO_FILESYSTEM, &attributes);
        QString display;
        PWSTR text = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &text)) && text) {
          display = QString::fromWCharArray(text);
          CoTaskMemFree(text);
        }
        QString path;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &text)) && text) {
          path = QString::fromWCharArray(text);
          CoTaskMemFree(text);
        } else if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &text)) && text) {
          path = QString::fromWCharArray(text);
          CoTaskMemFree(text);
        }
        // Filesystem folders only; virtual folders are skipped except the Recycle Bin.
        const bool isFolder = (attributes & SFGAO_FOLDER) != 0;
        const bool isFileSystem = (attributes & SFGAO_FILESYSTEM) != 0;
        if (!path.isEmpty() && isFolder &&
            ((isFileSystem && QFileInfo(path).isDir()) ||
             path.toUpper().contains(QLatin1String(kRecycleBinClsid)))) {
          if (display.isEmpty()) display = QFileInfo(path).fileName();
          if (display.isEmpty()) display = path;
          folders.push_back({display, path});
        }
        item->Release();
        item = nullptr;
      }
      items->Release();
    }
    quickAccess->Release();
    return folders;
  }

 private:
  // The icon the shell shows for a shortcut: its own icon file when it names one, else the
  // target's.
  static IconBitmap shortcutIcon(const QString& path, int size) {
    if (const auto shortcut = readShortcut(path)) {
      if (!shortcut->iconFile.isEmpty()) {
        const QString file = expandEnvironment(shortcut->iconFile);
        if (QFileInfo(file).isFile()) {
          IconBitmap bitmap = extractedIcon(file, shortcut->iconIndex, size);
          if (!bitmap.isNull()) return bitmap;
        }
      }
      if (!shortcut->target.isEmpty() && QFileInfo(shortcut->target).isFile()) {
        IconBitmap bitmap = shellItemIcon(shortcut->target, size);
        if (!bitmap.isNull()) return bitmap;
      }
    }
    return shellItemIcon(path, size);
  }

  // Start menu shortcut whose target is the exe (best name match wins), as find_app_shortcut.
  std::optional<std::pair<QString, QString>> findAppShortcut(const QString& exe,
                                                             const QStringList& candidates) const {
    if (QFileInfo(exe).fileName().compare(QLatin1String("explorer.exe"), Qt::CaseInsensitive) == 0)
      return std::nullopt;
    const std::vector<QString> variants = normalizedTargets(exe);
    if (variants.empty()) return std::nullopt;
    for (const QString& key : variants) {
      const auto cached = shortcutCache_.find(key);
      if (cached == shortcutCache_.end()) continue;
      if (QFileInfo::exists(cached->second))
        return std::make_pair(cached->second, QFileInfo(cached->second).completeBaseName());
      shortcutCache_.erase(cached);
    }

    QString bestPath;
    QString bestName;
    int bestScore = -1;
    const QString exeName = QFileInfo(exe).completeBaseName().toLower();
    for (const QString& root : startMenuRoots()) {
      QDirIterator it(root, {QStringLiteral("*.lnk")}, QDir::Files, QDirIterator::Subdirectories);
      while (it.hasNext()) {
        const QString file = it.next();
        const auto shortcut = readShortcut(file);
        if (!shortcut || shortcut->target.isEmpty()) continue;
        const QString name = QFileInfo(file).completeBaseName();
        const QString lower = name.toLower();
        const bool match = std::find(variants.begin(), variants.end(),
                                     normalizePath(shortcut->target)) != variants.end() ||
                           exeName == lower;
        if (!match) continue;
        int score = 0;
        for (int index = 0; index < candidates.size(); ++index) {
          const QString term = candidates[index].toLower();
          if (lower == term)
            score = std::max(score, 100 - index * 10);
          else if (!term.isEmpty() && lower.contains(term))
            score = std::max(score, 70 - index * 10);
        }
        if (bestPath.isEmpty() || score > bestScore) {
          bestPath = file;
          bestName = name;
          bestScore = score;
        }
        if (bestScore >= 100) break;
      }
      if (bestScore >= 100) break;
    }
    if (bestPath.isEmpty()) return std::nullopt;
    for (const QString& key : variants) shortcutCache_[key] = bestPath;
    return std::make_pair(bestPath, bestName);
  }

  // Start menu shortcut whose file name matches one of the names, as find_shortcut_by_name.
  std::optional<std::pair<QString, QString>> findShortcutByName(const QStringList& names) const {
    QStringList keys;
    for (const QString& name : names) {
      const QString key = canonicalDisplayKey(name);
      if (!key.isEmpty()) keys << key;
    }
    if (keys.isEmpty()) return std::nullopt;
    for (const QString& key : keys) {
      const auto cached = nameCache_.find(key);
      if (cached == nameCache_.end()) continue;
      if (QFileInfo::exists(cached->second))
        return std::make_pair(cached->second, QFileInfo(cached->second).completeBaseName());
      nameCache_.erase(cached);
    }
    for (const QString& root : startMenuRoots()) {
      QDirIterator it(root, {QStringLiteral("*.lnk")}, QDir::Files, QDirIterator::Subdirectories);
      while (it.hasNext()) {
        const QString file = it.next();
        const QString stem = canonicalDisplayKey(QFileInfo(file).completeBaseName());
        if (stem.isEmpty() || !keys.contains(stem)) continue;
        nameCache_[stem] = file;
        return std::make_pair(file, QFileInfo(file).completeBaseName());
      }
    }
    return std::nullopt;
  }

  // Start menu shortcut that carries the AUMID, which is how Windows pins an app.
  static std::optional<std::pair<QString, QString>> findShortcutByAumid(const QString& aumid) {
    if (aumid.isEmpty()) return std::nullopt;
    for (const QString& root : startMenuRoots()) {
      QDirIterator it(root, {QStringLiteral("*.lnk")}, QDir::Files, QDirIterator::Subdirectories);
      while (it.hasNext()) {
        const QString file = it.next();
        if (shortcutAumid(file) == aumid)
          return std::make_pair(file, QFileInfo(file).completeBaseName());
      }
    }
    return std::nullopt;
  }

  // Normalized exe path -> shortcut, and canonical shortcut name -> shortcut. GUI thread only.
  mutable std::map<QString, QString> shortcutCache_;
  mutable std::map<QString, QString> nameCache_;
};

}  // namespace

UniqueIdParts parseUniqueId(const QString& uniqueId) {
  const qsizetype colon = uniqueId.indexOf(QLatin1Char(':'));
  if (colon < 0) return {QStringLiteral("unknown"), uniqueId, QString()};
  UniqueIdParts parts;
  parts.type = uniqueId.left(colon);
  QString rest = uniqueId.mid(colon + 1);
  const qsizetype pipe = rest.indexOf(QLatin1Char('|'));
  if (pipe >= 0) {
    parts.args = rest.mid(pipe + 1);
    rest.truncate(pipe);
  }
  parts.value = rest;
  return parts;
}

QString toolFileFromArguments(const QString& arguments, const QString& system32) {
  if (arguments.trimmed().isEmpty()) return {};
  // shlex.split(posix=False): white space separates, double quotes group and stay in the token.
  QStringList tokens;
  QString current;
  bool quoted = false;
  for (const QChar ch : arguments) {
    if (ch == QLatin1Char('"')) quoted = !quoted;
    if (ch.isSpace() && !quoted) {
      if (!current.isEmpty()) tokens << current;
      current.clear();
    } else {
      current += ch;
    }
  }
  if (!current.isEmpty()) tokens << current;

  for (QString token : tokens) {
    if (token.startsWith(QLatin1Char('"'))) token.remove(0, 1);
    if (token.endsWith(QLatin1Char('"'))) token.chop(1);
    for (const QString& part : token.split(QLatin1Char(','))) {
      const QString lower = part.toLower();
      if (!lower.endsWith(QLatin1String(".msc")) && !lower.endsWith(QLatin1String(".cpl")))
        continue;
      const QString path =
          QFileInfo(part).isAbsolute() ? part : system32 + QLatin1Char('\\') + part;
      if (QFileInfo(path).isFile()) return path;
    }
  }
  return {};
}

bool isRecycleBinId(const QString& uniqueId) {
  return uniqueId.toUpper().contains(QLatin1String(kRecycleBinClsid));
}

std::optional<ShortcutInfo> readShortcut(const QString& path) {
  ComScope com;
  IShellLinkW* link = nullptr;
  if (FAILED(
          CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
      !link) {
    return std::nullopt;
  }
  std::optional<ShortcutInfo> result;
  IPersistFile* file = nullptr;
  if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))) && file) {
    if (SUCCEEDED(file->Load(wide(path), STGM_READ))) {
      ShortcutInfo info;
      wchar_t buffer[MAX_PATH * 4] = {};
      if (SUCCEEDED(link->GetPath(buffer, static_cast<int>(std::size(buffer)), nullptr, 0)))
        info.target = QString::fromWCharArray(buffer);
      buffer[0] = L'\0';
      if (SUCCEEDED(link->GetArguments(buffer, static_cast<int>(std::size(buffer)))))
        info.arguments = QString::fromWCharArray(buffer);
      buffer[0] = L'\0';
      if (SUCCEEDED(link->GetWorkingDirectory(buffer, static_cast<int>(std::size(buffer)))))
        info.workingDirectory = QString::fromWCharArray(buffer);
      buffer[0] = L'\0';
      int index = 0;
      if (SUCCEEDED(link->GetIconLocation(buffer, static_cast<int>(std::size(buffer)), &index))) {
        info.iconFile = QString::fromWCharArray(buffer);
        info.iconIndex = index;
      }
      result = info;
    }
    file->Release();
  }
  link->Release();
  return result;
}

std::shared_ptr<PinSupport> makeWin32PinSupport() { return std::make_shared<Win32PinSupport>(); }

}  // namespace yasb::platform
