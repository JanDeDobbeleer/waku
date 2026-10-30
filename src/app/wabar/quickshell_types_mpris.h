#pragma once

// The `Quickshell.Services.Mpris` module's types: the media feature's, registered again under
// Quickshell's names so that `import Quickshell.Services.Mpris` shows these names and no others of
// wabar.services. `Mpris` is the same singleton object as wabar.services' one.

#include <QtQml/qqmlregistration.h>

#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>

#include "features/media/qml/mpris.h"

namespace wabar::quickshell {

struct QsMprisLoopState {
  Q_GADGET
  QML_FOREIGN(wabar::media::MprisLoopState)
  QML_NAMED_ELEMENT(MprisLoopState)
  QML_UNCREATABLE("MprisLoopState only holds the enum")
};

struct QsMprisPlaybackState {
  Q_GADGET
  QML_FOREIGN(wabar::media::MprisPlaybackState)
  QML_NAMED_ELEMENT(MprisPlaybackState)
  QML_UNCREATABLE("MprisPlaybackState only holds the enum")
};

struct QsMprisPlayer {
  Q_GADGET
  QML_FOREIGN(wabar::media::MprisPlayer)
  QML_NAMED_ELEMENT(MprisPlayer)
  QML_UNCREATABLE("MprisPlayer objects come from Mpris.players")
};

struct QsMpris {
  Q_GADGET
  QML_FOREIGN(wabar::media::Mpris)
  QML_NAMED_ELEMENT(Mpris)
  QML_SINGLETON

 public:
  // The singleton of wabar.services, so both names share one session list.
  static wabar::media::Mpris* create(QQmlEngine* engine, QJSEngine*) {
    auto* mpris = engine->singletonInstance<wabar::media::Mpris*>(QStringLiteral("wabar.services"),
                                                                  QStringLiteral("Mpris"));
    QJSEngine::setObjectOwnership(mpris, QJSEngine::CppOwnership);
    return mpris;
  }
};

}  // namespace wabar::quickshell
