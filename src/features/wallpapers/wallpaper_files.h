#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <nlohmann/json_fwd.hpp>

#include "features/wallpapers/desktop_wallpaper.h"

namespace wabar::wallpapers {

// Port of core/widgets/services/wallpapers/images.py (folder scanning half).

// `image_path` is a string or a list of strings in the config.
QStringList imagePathsFrom(const nlohmann::json& imagePath);

// Extensions Windows accepts as a wallpaper on this build; webp needs build 26220.7653 or
// 26100.8037.
QStringList supportedImageTypes(const WindowsBuild& build);
QStringList supportedImageTypes();

// Every usable wallpaper under `paths`, in Explorer name order (StrCmpLogicalW). Like Python the
// match is a lower-case `endswith` on the bare extension, so "xpng" matches too.
QStringList collectImageFiles(const QStringList& paths, const QStringList& types);
QStringList collectImageFiles(const QStringList& paths);

// Where each screen's current wallpaper sits in `files`, by screen name. Paths compare as Windows
// does (case-insensitive, normalised).
QHash<QString, int> findWallpapers(const QStringList& files,
                                   const QHash<QString, QString>& wallpapers);

// os.path.normcase(os.path.normpath(path)).
QString normalizedPathKey(const QString& path);

}  // namespace wabar::wallpapers
