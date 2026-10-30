#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace wabar::notifications {

// One toast as the Action Center lists it (services/notifications/windows_notification.py
// NotificationItem). Image fields are local files, empty when the toast has none or it is not on
// this PC.
struct NotificationItem {
  std::uint32_t id = 0;
  QString aumid;
  QString appName;
  QString title;
  QString body;
  QDateTime createdAt;
  QString launch;
  QString activationType = QStringLiteral("foreground");
  QString attribution;
  QString logo;
  bool logoCircle = false;
  QString hero;
  QString image;
  QString
      scenario;  // the toast's `scenario` attribute: default, reminder, alarm, incomingCall, urgent

  bool operator==(const NotificationItem&) const = default;
};

struct ToastImage {
  QString placement;
  QString src;
  QString hintCrop;
};

// The parts of a toast's XML payload the list needs (pure parse, no OS access).
struct ToastXml {
  QStringList texts;  // binding texts without the attribution, stripped, non-empty
  QString attribution;
  QString launch;
  QString activationType = QStringLiteral("foreground");
  QString scenario;
  std::optional<QDateTime> displayTimestamp;
  QString bindingTemplate;
  bool hasBinding = false;
  std::vector<ToastImage> images;  // the first binding's own <image> children, in order

  // The binding's first image with this placement (port of _find_image, including the
  // ToastImageAndText templates), or null.
  const ToastImage* findImage(const QString& placement) const;
  // The first image without a placement (the inline image), or null.
  const ToastImage* findInlineImage() const;
};

// nullopt for XML that does not parse.
std::optional<ToastXml> parseToastXml(const QString& xml);

// Pops the first toast whose texts equal `texts` (the history has no ids; port of _pop_toast).
std::optional<ToastXml> popToast(std::vector<ToastXml>& toasts, const QStringList& texts);

// Where the image resolver finds things; every member may be empty. Tests substitute fakes.
struct ImagePathEnv {
  QString family;
  QString localAppData;
  std::function<QString()> installFolder;                  // ms-appx root of the package
  std::function<QString(const QString&)> downloadedImage;  // wpnidm lookup for a web URL
};

// Port of _get_image_path: the local file an image src points to, empty for a web image Windows
// did not download, a missing file, a network share or a path outside the allowed root.
QString resolveToastImagePath(const ToastImage* image, const ImagePathEnv& env);

// One listener notification before the toast XML is joined to it.
struct RawToast {
  std::uint32_t id = 0;
  QString aumid;
  QString appName;
  QString family;
  QDateTime created;
  QStringList texts;
};

// Port of _read_items. `raw` is newest first. readHistory(aumid, count) returns the newest `count`
// parsed toasts of the app, newest first; envFor(aumid, family) builds the image environment.
std::vector<NotificationItem> assembleItems(
    const std::vector<RawToast>& raw,
    const std::function<std::vector<ToastXml>(const QString&, int)>& readHistory,
    const std::function<ImagePathEnv(const QString&, const QString&)>& envFor);

}  // namespace wabar::notifications
