#pragma once

// A fake for the thumbnail seam (host/thumbnail.h). Header-only; tests only. It registers nothing
// on the machine.

#include <QSize>
#include <QStringList>
#include <map>

#include "host/thumbnail.h"

namespace waku::host {

class FakeThumbnailBackend : public ThumbnailBackend {
 public:
  struct Entry {
    quintptr destination = 0;
    quintptr source = 0;
    ThumbnailProps props;
    int updates = 0;
  };

  quintptr registerThumbnail(quintptr destination, quintptr source) override {
    calls << QStringLiteral("register");
    if (failRegistration || source == 0) return 0;
    const quintptr handle = nextHandle_++;
    entries[handle] = Entry{destination, source, {}, 0};
    return handle;
  }
  void unregisterThumbnail(quintptr handle) override {
    if (handle == 0) return;
    calls << QStringLiteral("unregister");
    entries.erase(handle);
  }
  QSize sourceSize(quintptr handle) override { return entries.count(handle) ? size : QSize(); }
  bool update(quintptr handle, const ThumbnailProps& props) override {
    calls << QStringLiteral("update");
    const auto found = entries.find(handle);
    if (found == entries.end()) return false;
    found->second.props = props;
    ++found->second.updates;
    return true;
  }

  // The only live entry (most tests have one).
  const Entry* only() const { return entries.size() == 1 ? &entries.begin()->second : nullptr; }

  bool failRegistration = false;
  QSize size = QSize(1920, 1080);  // what every registered source reports
  QStringList calls;               // register / unregister / update, in order
  std::map<quintptr, Entry> entries;

 private:
  quintptr nextHandle_ = 100;
};

}  // namespace waku::host
