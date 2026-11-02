#include "features/apps/qml/desktop_entry.h"

#include "features/apps/qml/app_icon_provider.h"

namespace waku::apps {

DesktopAction::DesktopAction(QString id, QString name, std::function<void()> run, QObject* parent)
    : QObject(parent), id_(std::move(id)), name_(std::move(name)), run_(std::move(run)) {}

void DesktopAction::execute() {
  if (run_) run_();
}

DesktopEntry::DesktopEntry(AppRecord record, Launch launch, QObject* parent)
    : QObject(parent), record_(std::move(record)), launch_(std::move(launch)) {
  rebuildActions();
}

QString DesktopEntry::icon() const { return appIconUrl(record_.id); }

void DesktopEntry::setRecord(AppRecord record) {
  if (record == record_) return;
  record_ = std::move(record);
  rebuildActions();
  emit changed();
}

void DesktopEntry::rebuildActions() {
  qDeleteAll(actions_);
  actions_.clear();
  const bool shortcut = record_.kind == AppKind::Shortcut;
  if (!shortcut && record_.elevationAlias.isEmpty()) return;
  const QString id = record_.id;
  actions_.append(new DesktopAction(
      QStringLiteral("admin"), QStringLiteral("Run as administrator"),
      [this, id]() {
        if (launch_) launch_(id, LaunchMode::Admin);
      },
      this));
  if (!shortcut) return;
  actions_.append(new DesktopAction(
      QStringLiteral("location"), QStringLiteral("Open file location"),
      [this, id]() {
        if (launch_) launch_(id, LaunchMode::OpenLocation);
      },
      this));
}

QQmlListProperty<DesktopAction> DesktopEntry::actions() {
  return QQmlListProperty<DesktopAction>(
      this, &actions_,
      [](QQmlListProperty<DesktopAction>* list) {
        return static_cast<qsizetype>(static_cast<QList<DesktopAction*>*>(list->data)->size());
      },
      [](QQmlListProperty<DesktopAction>* list, qsizetype index) {
        return static_cast<QList<DesktopAction*>*>(list->data)->value(index);
      });
}

void DesktopEntry::execute() {
  if (launch_) launch_(record_.id, LaunchMode::Normal);
}

}  // namespace waku::apps
