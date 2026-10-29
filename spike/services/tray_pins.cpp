#include "spike/services/tray_pins.h"

#include <QDebug>
#include <algorithm>
#include <climits>

#include "kernel/qt/app_data.h"

namespace wabar::services {

namespace {

constexpr int kNoIndex = INT_MAX;

bool contains(const std::vector<int>& ids, int id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

// --- TrayPinList ---

TrayPinList::TrayPinList(QObject* parent) : QAbstractListModel(parent) {}

int TrayPinList::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(ids_.size());
}

QVariant TrayPinList::data(const QModelIndex& index, int role) const {
  if (!source_ || !index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(ids_.size())) {
    return {};
  }
  const int id = ids_[static_cast<size_t>(index.row())];
  if (role == TrayIconModel::IdRole) return id;
  const int at = source_->rowOfId(id);
  if (at < 0) return {};
  return source_->data(source_->index(at), role);
}

QHash<int, QByteArray> TrayPinList::roleNames() const { return TrayIconModel().roleNames(); }

int TrayPinList::rowOfId(int id) const {
  const auto it = std::find(ids_.begin(), ids_.end(), id);
  return it == ids_.end() ? -1 : static_cast<int>(it - ids_.begin());
}

void TrayPinList::insertAt(int row, int id) {
  row = std::clamp(row, 0, static_cast<int>(ids_.size()));
  beginInsertRows({}, row, row);
  ids_.insert(ids_.begin() + row, id);
  endInsertRows();
  emit countChanged();
}

void TrayPinList::removeId(int id) {
  const int row = rowOfId(id);
  if (row < 0) return;
  beginRemoveRows({}, row, row);
  ids_.erase(ids_.begin() + row);
  endRemoveRows();
  emit countChanged();
}

void TrayPinList::moveRow(int from, int to) {
  if (from == to) return;
  beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
  const int id = ids_[static_cast<size_t>(from)];
  ids_.erase(ids_.begin() + from);
  ids_.insert(ids_.begin() + to, id);
  endMoveRows();
}

void TrayPinList::reset(std::vector<int> ids) {
  const bool countDiffers = ids.size() != ids_.size();
  beginResetModel();
  ids_ = std::move(ids);
  endResetModel();
  if (countDiffers) emit countChanged();
}

void TrayPinList::sourceChanged(int id) {
  const int row = rowOfId(id);
  if (row < 0) return;
  const QModelIndex idx = index(row);
  emit dataChanged(idx, idx);
}

// --- TrayPins ---

TrayPins::TrayPins(QObject* parent) : QObject(parent), pinned_(this), unpinned_(this) {}

void TrayPins::setIcons(TrayIconModel* icons) {
  if (icons_ || !icons) return;
  icons_ = icons;
  pinned_.setSource(icons);
  unpinned_.setSource(icons);
  connect(icons, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex&, int a, int b) {
    try {
      onRowsInserted(a, b);
    } catch (const std::exception& error) {
      qWarning().noquote() << "Tray pins: could not place icons:" << error.what();
    }
  });
  connect(icons, &QAbstractItemModel::rowsRemoved, this, [this] { onRowsRemoved(); });
  connect(icons, &QAbstractItemModel::dataChanged, this,
          [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
            for (int r = topLeft.row(); r <= bottomRight.row(); ++r) {
              const int id = icons_->data(icons_->index(r), TrayIconModel::IdRole).toInt();
              pinned_.sourceChanged(id);
              unpinned_.sourceChanged(id);
            }
          });
  reload();  // places the rows already there
  emit iconsChanged();
}

QString TrayPins::effectiveScreenId() const {
  return screenId_.isEmpty() ? QStringLiteral("None") : screenId_;
}

void TrayPins::setScreenId(const QString& id) {
  if (id == screenId_ && loaded_) return;
  const bool changed = id != screenId_;
  screenId_ = id;
  reload();
  if (changed) emit screenIdChanged();
}

void TrayPins::setScreen(QScreen* screen) {
  const bool changed = screen != screen_.data();
  screen_ = screen;
  setScreenId(screen ? yasb::widgets::cleanScreenId(screen->manufacturer(), screen->name(),
                                                    screen->serialNumber())
                     : QStringLiteral("None"));
  if (changed) emit screenChanged();
}

void TrayPins::setWindow(QWindow* window) {
  if (window == window_.data()) return;
  if (window_) disconnect(window_, &QWindow::screenChanged, this, nullptr);
  window_ = window;
  if (window) {
    connect(window, &QWindow::screenChanged, this, [this](QScreen* screen) { setScreen(screen); });
  }
  setScreen(window ? window->screen() : nullptr);
  emit windowChanged();
}

void TrayPins::setStateDir(const QString& dir) {
  if (dir == stateDir_) return;
  stateDir_ = dir;
  if (loaded_) reload();
  emit stateDirChanged();
}

void TrayPins::reload() {
  try {
    if (stateDir_.isEmpty()) stateDir_ = kernel::appDataDir();
    const yasb::widgets::SystrayStateStore store(stateDir_, effectiveScreenId());
    currentState_ = store.load();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray pins: could not read the state file:" << error.what();
    currentState_ = {};
  }
  loaded_ = true;

  std::vector<int> pinned;
  std::vector<int> unpinned;
  if (icons_) {
    for (int r = 0; r < icons_->rowCount(); ++r) {
      const int id = icons_->data(icons_->index(r), TrayIconModel::IdRole).toInt();
      const platform::TrayIconData* data = icons_->iconData(id);
      if (!data) continue;
      std::vector<int>& list =
          yasb::widgets::savedStateFor(currentState_, *data).isPinned ? pinned : unpinned;
      list.insert(list.begin() + positionFor(list, id), id);
    }
  }
  pinned_.reset(std::move(pinned));
  unpinned_.reset(std::move(unpinned));
}

int TrayPins::sortKey(int id) const {
  const platform::TrayIconData* data = icons_ ? icons_->iconData(id) : nullptr;
  if (!data) return kNoIndex;
  const int index = yasb::widgets::savedStateFor(currentState_, *data).index;
  return index < 0 ? kNoIndex : index;
}

// After every peer whose saved index is not greater: icons without one go last, ties keep
// arrival order.
int TrayPins::positionFor(const std::vector<int>& list, int id) const {
  const int key = sortKey(id);
  int pos = 0;
  while (pos < static_cast<int>(list.size()) && sortKey(list[static_cast<size_t>(pos)]) <= key)
    ++pos;
  return pos;
}

void TrayPins::onRowsInserted(int first, int last) {
  if (!icons_) return;
  for (int r = first; r <= last; ++r) {
    const int id = icons_->data(icons_->index(r), TrayIconModel::IdRole).toInt();
    if (contains(pinned_.ids(), id) || contains(unpinned_.ids(), id)) continue;
    const platform::TrayIconData* data = icons_->iconData(id);
    if (!data) continue;
    TrayPinList& list =
        yasb::widgets::savedStateFor(currentState_, *data).isPinned ? pinned_ : unpinned_;
    list.insertAt(positionFor(list.ids(), id), id);
  }
}

void TrayPins::onRowsRemoved() {
  if (!icons_) return;
  for (TrayPinList* list : {&pinned_, &unpinned_}) {
    const std::vector<int> ids = list->ids();
    for (const int id : ids) {
      if (icons_->rowOfId(id) < 0) list->removeId(id);
    }
  }
}

QString TrayPins::keyOf(int id) const {
  const platform::TrayIconData* data = icons_ ? icons_->iconData(id) : nullptr;
  return data ? yasb::widgets::iconStateKey(*data) : QString();
}

QStringList TrayPins::keysOf(const std::vector<int>& ids) const {
  QStringList keys;
  for (const int id : ids) {
    if (!icons_ || !icons_->iconData(id)) continue;
    const QString key = keyOf(id);
    if (!keys.contains(key)) keys.append(key);
  }
  return keys;
}

bool TrayPins::isPinned(int id) const { return contains(pinned_.ids(), id); }

void TrayPins::setPinned(int id, bool pinned) {
  if (!contains(pinned_.ids(), id) && !contains(unpinned_.ids(), id)) return;
  if (isPinned(id) == pinned) return;
  applyMove(id, pinned, INT_MAX);
}

void TrayPins::move(int id, bool pinned, int toIndex) {
  if (!contains(pinned_.ids(), id) && !contains(unpinned_.ids(), id)) return;
  applyMove(id, pinned, toIndex);
}

void TrayPins::applyMove(int id, bool pinned, int toIndex) {
  TrayPinList& from = isPinned(id) ? pinned_ : unpinned_;
  TrayPinList& to = pinned ? pinned_ : unpinned_;
  if (&from == &to) {
    const int row = from.rowOfId(id);
    const int target = std::clamp(toIndex, 0, static_cast<int>(from.ids().size()) - 1);
    if (target == row) return;
    from.moveRow(row, target);
  } else {
    const QString key = keyOf(id);
    from.removeId(id);
    int at = std::clamp(toIndex, 0, static_cast<int>(to.ids().size()));
    to.insertAt(at, id);
    // Icons of one program share one saved state, so they change side together.
    for (const int other : std::vector<int>(from.ids())) {
      if (keyOf(other) != key) continue;
      from.removeId(other);
      to.insertAt(++at, other);
    }
  }
  persist();
}

void TrayPins::moveBy(int id, int delta) {
  TrayPinList& list = isPinned(id) ? pinned_ : unpinned_;
  const int row = list.rowOfId(id);
  if (row < 0) return;
  const int target = std::clamp(row + delta, 0, static_cast<int>(list.ids().size()) - 1);
  if (target == row) return;
  list.moveRow(row, target);
  persist();
}

void TrayPins::persist() {
  try {
    yasb::widgets::updateCurrentState(currentState_, keysOf(pinned_.ids()),
                                      keysOf(unpinned_.ids()));
    const yasb::widgets::SystrayStateStore store(stateDir_, effectiveScreenId());
    if (!store.save(currentState_))
      qWarning().noquote() << "Tray pins: state could not be written to" << store.path();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Tray pins: state could not be saved:" << error.what();
  }
}

}  // namespace wabar::services
