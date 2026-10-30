#include "features/color_scheme/qml/color_scheme_service.h"

#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>
#include <exception>
#include <optional>

#include "features/color_scheme/dynamic_scheme.h"
#include "features/color_scheme/image_source.h"

namespace wabar::color_scheme {

namespace {

constexpr int kLuminanceSide = 64;
// The scheme variant used when the caller names none (caelestia's default).
constexpr Variant kDefaultVariant = Variant::TonalSpot;

QColor toQColor(Argb argb) { return QColor(redOf(argb), greenOf(argb), blueOf(argb)); }

QString toLocalPath(const QString& pathOrUrl) {
  if (pathOrUrl.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
    return QUrl(pathOrUrl).toLocalFile();
  }
  return pathOrUrl;
}

std::optional<QColor> parseColor(const QVariant& value) {
  QColor color;
  if (value.metaType().id() == QMetaType::QColor) {
    color = value.value<QColor>();
  } else if (value.canConvert<QString>()) {
    color = QColor::fromString(value.toString());
  }
  if (!color.isValid()) return std::nullopt;
  return color;
}

// Builds the QML value from a scheme: each role under its plain name.
SchemeValue toValue(const DynamicScheme& scheme) {
  SchemeValue value;
  value.valid = true;
  value.variant = QString::fromUtf8(variantName(scheme.variant).data(),
                                    static_cast<qsizetype>(variantName(scheme.variant).size()));
  value.dark = scheme.dark;
  value.contrast = scheme.contrast;
  value.source = toQColor(scheme.source.argb());
  for (int i = 0; i < static_cast<int>(Role::Count_); ++i) {
    const Role role = static_cast<Role>(i);
    const std::string_view name = roleName(role);
    const QString key = QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    const QColor color = toQColor(roleColor(role, scheme));
    value.colors.insert(key, color);
  }
  return value;
}

// Computed off the GUI thread: the scheme of an image file, or an error text.
struct ImageResult {
  SchemeValue scheme;
  QString error;
};

ImageResult schemeFromImageFile(const QString& path, Variant variant, bool dark, double contrast) {
  ImageResult result;
  try {
    QString error;
    const QImage image = readImageScaled(path, 128, &error);
    if (image.isNull()) {
      result.error = error.isEmpty() ? QStringLiteral("cannot read image") : error;
      return result;
    }
    const std::optional<Argb> seed = seedColorFromImage(image);
    if (!seed) {
      result.error = QStringLiteral("empty image");
      return result;
    }
    result.scheme = toValue(makeScheme(*seed, variant, dark, contrast));
  } catch (const std::exception& e) {
    result.error = QString::fromUtf8(e.what());
  } catch (...) {
    result.error = QStringLiteral("unexpected error");
  }
  return result;
}

}  // namespace

ColorSchemeService::ColorSchemeService(QObject* parent) : QObject(parent) {}

QStringList ColorSchemeService::variants() const {
  QStringList names;
  for (const Variant variant : allVariants()) {
    const std::string_view name = variantName(variant);
    names.append(QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())));
  }
  return names;
}

QString ColorSchemeService::variantOrDefault(const QString& variant) const {
  if (variant.trimmed().isEmpty()) {
    return QString::fromUtf8(variantName(kDefaultVariant).data());
  }
  return variant;
}

SchemeValue ColorSchemeService::fromColor(const QVariant& color, const QString& variant, bool dark,
                                          double contrast) const {
  try {
    const std::optional<Variant> parsed = parseVariant(variantOrDefault(variant).toStdString());
    const std::optional<QColor> seed = parseColor(color);
    if (!parsed || !seed) return {};
    const Argb argb = argbFromRgb(seed->red(), seed->green(), seed->blue());
    return toValue(makeScheme(argb, *parsed, dark, contrast));
  } catch (const std::exception&) {
    return {};
  }
}

void ColorSchemeService::fromImage(const QString& path, const QString& variant, bool dark,
                                   double contrast) {
  const std::optional<Variant> parsed = parseVariant(variantOrDefault(variant).toStdString());
  if (!parsed) {
    emit fromImageFailed(path, QStringLiteral("unknown variant '%1'").arg(variant));
    return;
  }
  const QString local = toLocalPath(path);
  const Variant chosen = *parsed;
  QPointer<ColorSchemeService> self(this);
  QThreadPool::globalInstance()->start([self, path, local, chosen, dark, contrast] {
    ImageResult result = schemeFromImageFile(local, chosen, dark, contrast);
    if (self.isNull()) return;
    QMetaObject::invokeMethod(
        self.data(),
        [self, path, result = std::move(result)] {
          if (self.isNull()) return;
          if (result.error.isEmpty()) {
            emit self->fromImageFinished(path, result.scheme);
          } else {
            emit self->fromImageFailed(path, result.error);
          }
        },
        Qt::QueuedConnection);
  });
}

SchemeValue ColorSchemeService::generate(const QVariant& source, const QString& variant, bool dark,
                                         double contrast) {
  if (source.metaType().id() == QMetaType::QColor)
    return fromColor(source, variant, dark, contrast);
  const QString text = source.toString();
  if (QFileInfo(toLocalPath(text)).isFile()) {
    fromImage(text, variant, dark, contrast);
    return {};
  }
  if (parseColor(source)) return fromColor(source, variant, dark, contrast);
  emit fromImageFailed(text, QStringLiteral("not a colour or an image file"));
  return {};
}

double ColorSchemeService::luminance(const QString& path) const {
  const QImage image = readImageScaled(toLocalPath(path), kLuminanceSide);
  return image.isNull() ? -1.0 : meanLuminance(image);
}

QColor ColorSchemeService::seedColor(const QString& path) const {
  const QImage image = readImageScaled(toLocalPath(path), 128);
  if (image.isNull()) return {};
  const std::optional<Argb> seed = seedColorFromImage(image);
  return seed ? toQColor(*seed) : QColor();
}

}  // namespace wabar::color_scheme
