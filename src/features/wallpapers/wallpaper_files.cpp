#include "features/wallpapers/wallpaper_files.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace waku::wallpapers {
namespace {

// os.walk order: the files of a directory first, then each sub-directory in turn. Symlinked
// directories are listed but not entered (followlinks=False).
void walk(const QString& root, const QStringList& types, QStringList& out) {
  const QDir dir(root);
  const QFileInfoList entries = dir.entryInfoList(
      QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::NoSort);
  for (const QFileInfo& info : entries) {
    if (info.isDir()) continue;
    const QString name = info.fileName();
    const QString lower = name.toLower();
    for (const QString& type : types) {
      if (lower.endsWith(type)) {
        out.append(QDir::toNativeSeparators(info.absoluteFilePath()));
        break;
      }
    }
  }
  for (const QFileInfo& info : entries) {
    if (info.isDir() && !info.isSymLink()) walk(info.absoluteFilePath(), types, out);
  }
}

}  // namespace

QStringList imagePathsFrom(const nlohmann::json& imagePath) {
  QStringList result;
  if (imagePath.is_string()) {
    result << QString::fromStdString(imagePath.get<std::string>());
  } else if (imagePath.is_array()) {
    for (const auto& item : imagePath) {
      if (item.is_string()) result << QString::fromStdString(item.get<std::string>());
    }
  }
  return result;
}

QStringList supportedImageTypes(const WindowsBuild& build) {
  QStringList types{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                    QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("tif"),
                    QStringLiteral("tiff")};
  const bool webp =
      (build.build >= 26220 && build.ubr >= 7653) || (build.build >= 26100 && build.ubr >= 8037);
  if (webp) types << QStringLiteral("webp");
  return types;
}

QStringList supportedImageTypes() { return supportedImageTypes(windowsBuildAndUbr()); }

QStringList collectImageFiles(const QStringList& paths, const QStringList& types) {
  QStringList files;
  for (const QString& path : paths) {
    if (!QFileInfo::exists(path)) continue;
    if (QFileInfo(path).isDir()) walk(path, types, files);
  }
  // Python's sorted() is stable; so is this.
  std::stable_sort(files.begin(), files.end(),
                   [](const QString& a, const QString& b) { return compareLogical(a, b) < 0; });
  return files;
}

QStringList collectImageFiles(const QStringList& paths) {
  return collectImageFiles(paths, supportedImageTypes());
}

QString normalizedPathKey(const QString& path) {
  if (path.isEmpty()) return QStringLiteral(".");
  return QDir::toNativeSeparators(QDir::cleanPath(path)).toLower();
}

QHash<QString, int> findWallpapers(const QStringList& files,
                                   const QHash<QString, QString>& wallpapers) {
  QHash<QString, QStringList> wanted;
  for (auto it = wallpapers.cbegin(); it != wallpapers.cend(); ++it) {
    if (!it.value().isEmpty()) wanted[normalizedPathKey(it.value())].append(it.key());
  }
  QHash<QString, int> found;
  for (int index = 0; index < files.size(); ++index) {
    if (wanted.isEmpty()) break;
    const auto hit = wanted.find(normalizedPathKey(files[index]));
    if (hit == wanted.end()) continue;
    for (const QString& name : hit.value()) found.insert(name, index);
    wanted.erase(hit);
  }
  return found;
}

}  // namespace waku::wallpapers
