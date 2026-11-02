#pragma once

// Types of the `Quickshell` module whose C++ lives in a feature: DesktopEntry, DesktopAction (apps)
// and ElapsedTimer (quickshell_io). They are registered here a second time under Quickshell's
// names, so `import Quickshell` shows these names and no others of waku.services. No code: each
// struct only carries the registration (QML_FOREIGN). The file sits in app/ because only the
// composition root may see several features; the CMake of the Quickshell module lists it.

#include <QtQml/qqmlregistration.h>

#include <QObject>

#include "features/apps/qml/desktop_entry.h"
#include "features/quickshell_io/qml/quickshell_singleton.h"

namespace waku::quickshell {

struct QsDesktopAction {
  Q_GADGET
  QML_FOREIGN(waku::apps::DesktopAction)
  QML_NAMED_ELEMENT(DesktopAction)
  QML_UNCREATABLE("DesktopAction objects come from DesktopEntry.actions")
};

struct QsDesktopEntry {
  Q_GADGET
  QML_FOREIGN(waku::apps::DesktopEntry)
  QML_NAMED_ELEMENT(DesktopEntry)
  QML_UNCREATABLE("DesktopEntry objects come from DesktopEntries")
};

struct QsElapsedTimer {
  Q_GADGET
  QML_FOREIGN(waku::quickshell_io::ElapsedTimer)
  QML_NAMED_ELEMENT(ElapsedTimer)
};

}  // namespace waku::quickshell
