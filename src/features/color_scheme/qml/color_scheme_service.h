#pragma once

#include <QtQml/qqmlregistration.h>

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <memory>

#include "features/color_scheme/image_stats_cache.h"
#include "features/color_scheme/qml/color_scheme_types.h"

namespace wabar::color_scheme {

// The `ColorScheme` singleton (wabar.services): Material 3 dynamic colour with no external tool.
// What a shell otherwise gets from an external scheme tool and an image analyser, in process:
//
//   ColorScheme.fromColor("#6750A4", "tonalspot", true).colors.primary
//   ColorScheme.fromImage(path, "vibrant", true)   // then onFromImageFinished: (path, scheme)
//   ColorScheme.luminance(path)                    // 0..1, or -1 when the file cannot be read
//
// `variant` is one of `variants` (tonalspot, vibrant, expressive, neutral, monochrome, fidelity,
// content, rainbow, fruitsalad; case and "_" "-" ignored). `contrast` is -1 .. 1.
//
// Threads: GUI thread owns the object. fromImage() decodes and quantizes on a QThreadPool thread;
// that task touches no QObject, and posts the result back through a QPointer to this object. No
// thread of its own, nothing to stop at exit (a task that outlives the object finds the QPointer
// null and drops its result).
class ColorSchemeService : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(ColorScheme)
  QML_SINGLETON
  Q_PROPERTY(QStringList variants READ variants CONSTANT FINAL)

 public:
  explicit ColorSchemeService(QObject* parent = nullptr);
  // `stats` remembers the seed colour and luminance of image files between calls and runs.
  explicit ColorSchemeService(std::shared_ptr<ImageStatsCache> stats, QObject* parent = nullptr);

  QStringList variants() const;

  // Synchronous. `color` is a colour or a "#rrggbb" string. An unknown variant or colour gives a
  // scheme with valid == false.
  Q_INVOKABLE wabar::color_scheme::SchemeValue fromColor(const QVariant& color,
                                                         const QString& variant = QString(),
                                                         bool dark = true,
                                                         double contrast = 0.0) const;

  // Asynchronous: seeds the scheme from the image's dominant colour. Emits fromImageFinished or
  // fromImageFailed exactly once. `path` is a file path or a file:/// URL.
  Q_INVOKABLE void fromImage(const QString& path, const QString& variant = QString(),
                             bool dark = true, double contrast = 0.0);

  // One entry point: a colour (QColor or "#rrggbb") answers synchronously; a path to an existing
  // image file starts fromImage() and returns an invalid scheme. Everything else fails with
  // fromImageFailed.
  Q_INVOKABLE wabar::color_scheme::SchemeValue generate(const QVariant& source,
                                                        const QString& variant = QString(),
                                                        bool dark = true, double contrast = 0.0);

  // Mean luminance of the image, 0 (black) .. 1 (white), on gamma-encoded sRGB with Rec. 709
  // weights; -1 when the file cannot be read. Decodes a reduced copy on the calling thread.
  Q_INVOKABLE double luminance(const QString& path) const;

  // The seed colour fromImage() would pick, synchronously. Invalid colour when unreadable.
  Q_INVOKABLE QColor seedColor(const QString& path) const;

 signals:
  void fromImageFinished(const QString& path, const wabar::color_scheme::SchemeValue& scheme);
  void fromImageFailed(const QString& path, const QString& error);

 private:
  std::shared_ptr<ImageStatsCache> stats_;
  QString variantOrDefault(const QString& variant) const;
};

}  // namespace wabar::color_scheme
