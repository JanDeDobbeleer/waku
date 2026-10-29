#include "spike/services/taskbar_model.h"

#include <QDebug>
#include <algorithm>
#include <exception>
#include <map>
#include <set>

namespace wabar::services {

TaskbarModel::TaskbarModel(QObject* parent) : QAbstractListModel(parent) {}

int TaskbarModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QHash<int, QByteArray> TaskbarModel::roleNames() const {
  return {
      {KeyRole, "key"},           {TitleRole, "title"},       {TooltipRole, "tooltip"},
      {IconUrlRole, "iconUrl"},   {CountRole, "windowCount"}, {PinnedRole, "pinned"},
      {RunningRole, "running"},   {ActiveRole, "active"},     {MinimizedRole, "minimized"},
      {FlashingRole, "flashing"}, {UniqueIdRole, "uniqueId"},
  };
}

QVariant TaskbarModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size()))
    return {};
  const Row& row = rows_[static_cast<size_t>(index.row())];
  switch (role) {
    case KeyRole:
      return row.key;
    case TitleRole:
      return row.title;
    case TooltipRole:
      return row.tooltip;
    case IconUrlRole:
      return row.iconUrl;
    case CountRole:
      return row.count;
    case PinnedRole:
      return row.pinned;
    case RunningRole:
      return row.running;
    case ActiveRole:
      return row.active;
    case MinimizedRole:
      return row.minimized;
    case FlashingRole:
      return row.flashing;
    case UniqueIdRole:
      return row.uniqueId;
    default:
      return {};
  }
}

// --- properties

void TaskbarModel::setService(TaskbarService* service) {
  if (service_ == service) return;
  if (service_) disconnect(service_, nullptr, this, nullptr);
  service_ = service;
  if (service_) {
    connect(service_, &TaskbarService::changed, this, &TaskbarModel::rebuild);
    service_->keepCloakedTasks(!showOnlyVisible_);
  }
  emit serviceChanged();
  setWindow(window_);
  rebuild();
}

void TaskbarModel::setWindow(QWindow* window) {
  if (window_ && window_ != window) disconnect(window_, nullptr, this, nullptr);
  const bool changed = window_ != window;
  window_ = window;
  if (window_ && changed) {
    connect(window_, &QWindow::screenChanged, this, [this] {
      updateMonitor();
      rebuild();
    });
  }
  if (changed) emit windowChanged();
  if (!window_ || !service_) return;
  // Later than the other properties of the widget: the tracker must know whether to keep cloaked
  // windows before it enumerates.
  QPointer<TaskbarModel> self(this);
  QMetaObject::invokeMethod(
      this,
      [self] {
        if (!self || !self->service_ || !self->window_) return;
        self->service_->keepCloakedTasks(!self->showOnlyVisible_);
        self->service_->attach(self->window_);
        self->updateMonitor();
        self->rebuild();
      },
      Qt::QueuedConnection);
}

#define TASKBAR_SETTER(Name, member, Type)   \
  void TaskbarModel::set##Name(Type value) { \
    if (member == value) return;             \
    member = value;                          \
    emit filterChanged();                    \
    rebuild();                               \
  }

TASKBAR_SETTER(Grouping, grouping_, bool)
TASKBAR_SETTER(StrictFiltering, strictFiltering_, bool)
TASKBAR_SETTER(IgnoreTitles, ignoreTitles_, const QStringList&)
TASKBAR_SETTER(IgnoreClasses, ignoreClasses_, const QStringList&)
TASKBAR_SETTER(IgnoreProcesses, ignoreProcesses_, const QStringList&)
#undef TASKBAR_SETTER

void TaskbarModel::setShowOnlyVisible(bool value) {
  if (showOnlyVisible_ == value) return;
  showOnlyVisible_ = value;
  if (service_) service_->keepCloakedTasks(!value);
  emit filterChanged();
  rebuild();
}

void TaskbarModel::setMonitorExclusive(bool value) {
  if (monitorExclusive_ == value) return;
  monitorExclusive_ = value;
  updateMonitor();
  emit filterChanged();
  rebuild();
}

void TaskbarModel::updateMonitor() {
  monitor_ = 0;
  if (!monitorExclusive_ || !service_ || !window_) return;
  try {
    monitor_ =
        service_->system()->monitorOfWindow(static_cast<platform::WindowHandle>(window_->winId()));
  } catch (const std::exception& error) {
    qWarning().noquote() << "Taskbar: monitor of the bar failed:" << error.what();
  }
}

// --- rows

bool TaskbarModel::shownNow(platform::WindowHandle hwnd) const {
  return std::any_of(rows_.begin(), rows_.end(), [hwnd](const Row& row) {
    return std::find(row.members.begin(), row.members.end(), hwnd) != row.members.end();
  });
}

// shouldShowWindow of TaskbarWidget.
bool TaskbarModel::visibleHere(const TaskbarWindow& w) const {
  const platform::WindowInfo& info = w.info;
  if (info.title.trimmed().isEmpty()) return false;
  if (!info.processName) return false;
  if (ignoreTitles_.contains(info.title)) return false;
  if (ignoreClasses_.contains(info.className)) return false;
  if (ignoreProcesses_.contains(*info.processName)) return false;
  if (strictFiltering_ && !info.canMinimize) return false;
  if (showOnlyVisible_ && info.isCloaked) return false;
  if (monitorExclusive_ && monitor_ != 0) {
    // An unknown (transient) monitor keeps existing buttons but does not add new ones.
    if (info.monitor == 0) return shownNow(w.hwnd);
    if (info.monitor != monitor_) return false;
  }
  return true;
}

std::vector<TaskbarModel::Row> TaskbarModel::compute() {
  std::vector<Row> result;
  if (!service_) return result;
  auto& pins = service_->pinStore();

  // The running buttons in order of arrival.
  std::vector<Row> running;
  std::map<QString, size_t> groups;
  for (const TaskbarWindow& w : service_->windows()) {
    if (!visibleHere(w)) continue;
    const bool grouped = grouping_ && w.uniqueId.has_value();
    if (grouped) {
      const auto group = groups.find(*w.uniqueId);
      if (group != groups.end()) {
        running[group->second].members.push_back(w.hwnd);
        continue;
      }
      groups[*w.uniqueId] = running.size();
    }
    Row row;
    row.key = grouped ? QStringLiteral("g:") + *w.uniqueId : QStringLiteral("w:%1").arg(w.hwnd);
    row.uniqueId = w.uniqueId.value_or(QString());
    row.members = {w.hwnd};
    row.running = true;
    running.push_back(std::move(row));
  }

  for (Row& row : running) {
    const TaskbarWindow* rep = nullptr;
    bool allMinimized = true;
    for (platform::WindowHandle hwnd : row.members) {
      const TaskbarWindow* w = service_->window(hwnd);
      if (!w) continue;
      if (!rep || (w->info.isActive && !rep->info.isActive)) rep = w;
      row.active = row.active || w->info.isActive;
      row.flashing = row.flashing || w->info.isFlashing;
      allMinimized = allMinimized && w->minimized;
    }
    row.flashing = row.flashing && !row.active;
    row.minimized = allMinimized && !row.active;
    row.count = static_cast<int>(row.members.size());
    row.iconKey = rep ? rep->hwnd : row.members.front();
    row.title = rep ? rep->info.title : QString();
    row.tooltip =
        row.count > 1 ? QStringLiteral("%1 (%2 windows)").arg(row.title).arg(row.count) : row.title;
    row.pinned = !row.uniqueId.isEmpty() && pins.contains(row.uniqueId);
  }

  // Pinned apps first, in the file's order; the first running button of an app takes its place.
  std::vector<bool> used(running.size(), false);
  for (const QString& id : pins.order()) {
    bool placed = false;
    for (size_t i = 0; i < running.size(); ++i) {
      if (used[i] || running[i].uniqueId != id) continue;
      used[i] = true;
      result.push_back(running[i]);
      placed = true;
      break;
    }
    if (placed) continue;
    Row row;
    row.key = QStringLiteral("p:") + id;
    row.uniqueId = id;
    row.iconKey = service_->pseudoFor(id);
    row.title = pins.title(id).isEmpty() ? id : pins.title(id);
    row.tooltip = row.title;
    row.pinned = true;
    result.push_back(std::move(row));
  }
  for (size_t i = 0; i < running.size(); ++i) {
    if (!used[i]) result.push_back(running[i]);
  }

  for (Row& row : result) {
    service_->iconImage(row.iconKey);  // starts the load; the revision follows it
    row.iconUrl = QStringLiteral("image://windowicon/%1/%2")
                      .arg(row.iconKey)
                      .arg(service_->iconRevision(row.iconKey));
  }
  return result;
}

void TaskbarModel::rebuild() {
  if (rebuilding_) return;
  rebuilding_ = true;
  try {
    apply(compute());
  } catch (const std::exception& error) {
    qWarning().noquote() << "Taskbar: rebuild failed:" << error.what();
  } catch (...) {
    qWarning() << "Taskbar: rebuild failed";
  }
  rebuilding_ = false;
}

void TaskbarModel::apply(std::vector<Row> rows) {
  const int before = rowCount();
  std::set<QString> keep;
  for (const Row& row : rows) keep.insert(row.key);

  for (int i = static_cast<int>(rows_.size()) - 1; i >= 0; --i) {
    if (keep.count(rows_[static_cast<size_t>(i)].key)) continue;
    beginRemoveRows({}, i, i);
    rows_.erase(rows_.begin() + i);
    endRemoveRows();
  }
  for (size_t i = 0; i < rows.size(); ++i) {
    const int at = static_cast<int>(i);
    if (i < rows_.size() && rows_[i].key == rows[i].key) {
      if (!(rows_[i] == rows[i])) {
        rows_[i] = std::move(rows[i]);
        emit dataChanged(index(at), index(at));
      }
      continue;
    }
    size_t from = i + 1;
    while (from < rows_.size() && rows_[from].key != rows[i].key) ++from;
    if (from < rows_.size()) {
      beginMoveRows({}, static_cast<int>(from), static_cast<int>(from), {}, at);
      Row moved = std::move(rows_[from]);
      rows_.erase(rows_.begin() + static_cast<long>(from));
      rows_.insert(rows_.begin() + at, std::move(moved));
      endMoveRows();
      if (!(rows_[i] == rows[i])) {
        rows_[i] = std::move(rows[i]);
        emit dataChanged(index(at), index(at));
      }
    } else {
      beginInsertRows({}, at, at);
      rows_.insert(rows_.begin() + at, std::move(rows[i]));
      endInsertRows();
    }
  }
  if (rowCount() != before) emit countChanged();
}

// --- actions

const TaskbarModel::Row* TaskbarModel::rowAt(int row) const {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return nullptr;
  return &rows_[static_cast<size_t>(row)];
}

void TaskbarModel::activate(int row) {
  const Row* r = rowAt(row);
  if (!r || !service_) return;
  if (!r->running) {
    service_->launchPinned(r->uniqueId);
  } else if (r->members.size() > 1) {
    service_->cycle(r->members);
  } else {
    service_->activate(r->members.front());
  }
}

void TaskbarModel::middleClick(int row) {
  const Row* r = rowAt(row);
  if (!r || !service_) return;
  if (r->running)
    service_->closeWindow(r->members.front());
  else
    service_->launchPinned(r->uniqueId);
}

void TaskbarModel::closeWindow(int row) {
  const Row* r = rowAt(row);
  if (r && service_ && r->running) service_->closeWindow(r->members.front());
}

void TaskbarModel::closeAll(int row) {
  const Row* r = rowAt(row);
  if (!r || !service_ || !r->running) return;
  const std::vector<platform::WindowHandle> members = r->members;  // closing changes the rows
  for (platform::WindowHandle hwnd : members) service_->closeWindow(hwnd);
}

void TaskbarModel::togglePin(int row) {
  const Row* r = rowAt(row);
  if (!r || !service_) return;
  if (r->pinned)
    service_->unpinApp(r->uniqueId);
  else if (r->running)
    service_->pinWindow(r->members.front());
}

void TaskbarModel::launch(int row) {
  const Row* r = rowAt(row);
  if (r && service_ && !r->uniqueId.isEmpty() && r->pinned) service_->launchPinned(r->uniqueId);
}

}  // namespace wabar::services
