#include "features/notifications/toast_xml.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrl>
#include <QXmlStreamReader>
#include <memory>

namespace waku::notifications {
namespace {

struct Node {
  QString name;
  QHash<QString, QString> attributes;
  QString text;  // text before the first child, as ElementTree's .text
  std::vector<std::unique_ptr<Node>> children;

  QString attr(const QString& key, const QString& fallback = {}) const {
    const auto found = attributes.constFind(key);
    return found == attributes.constEnd() ? fallback : found.value();
  }
  std::vector<const Node*> childrenNamed(const QString& childName) const {
    std::vector<const Node*> result;
    for (const auto& child : children)
      if (child->name == childName) result.push_back(child.get());
    return result;
  }
};

std::unique_ptr<Node> parseDocument(const QString& xml) {
  QXmlStreamReader reader(xml);
  std::unique_ptr<Node> root;
  std::vector<Node*> stack;
  while (!reader.atEnd()) {
    switch (reader.readNext()) {
      case QXmlStreamReader::StartElement: {
        auto node = std::make_unique<Node>();
        node->name = reader.name().toString();
        for (const auto& attribute : reader.attributes())
          node->attributes.insert(attribute.name().toString(), attribute.value().toString());
        Node* raw = node.get();
        if (stack.empty()) {
          if (root) return nullptr;
          root = std::move(node);
        } else {
          stack.back()->children.push_back(std::move(node));
        }
        stack.push_back(raw);
        break;
      }
      case QXmlStreamReader::EndElement:
        if (!stack.empty()) stack.pop_back();
        break;
      case QXmlStreamReader::Characters:
        if (!stack.empty() && stack.back()->children.empty())
          stack.back()->text += reader.text().toString();
        break;
      default:
        break;
    }
  }
  if (reader.hasError() || !root) return nullptr;
  return root;
}

bool isUnc(const QString& path) {
  return path.startsWith(QLatin1String("\\\\")) || path.startsWith(QLatin1String("//"));
}

QString normalized(const QString& path) { return QDir::cleanPath(path).toLower(); }

bool isUnder(const QString& path, const QString& root) {
  const QString a = normalized(path);
  const QString r = normalized(root);
  return a == r || a.startsWith(r + QLatin1Char('/'));
}

struct ParsedUrl {
  QString scheme;
  QString netloc;
  QString path;
};

// urllib.parse.urlparse, as far as the image rules need it.
ParsedUrl parseUrl(const QString& src) {
  ParsedUrl url;
  QString rest = src;
  static const QRegularExpression schemeRe(QStringLiteral("^([A-Za-z][A-Za-z0-9+.-]*):"));
  if (const auto match = schemeRe.match(rest); match.hasMatch()) {
    url.scheme = match.captured(1).toLower();
    rest = rest.mid(match.capturedLength());
  }
  if (rest.startsWith(QLatin1String("//"))) {
    rest = rest.mid(2);
    const qsizetype end = rest.indexOf(QRegularExpression(QStringLiteral("[/?#]")));
    url.netloc = end < 0 ? rest : rest.left(end);
    rest = end < 0 ? QString() : rest.mid(end);
  }
  if (const qsizetype hash = rest.indexOf(QLatin1Char('#')); hash >= 0) rest = rest.left(hash);
  if (const qsizetype query = rest.indexOf(QLatin1Char('?')); query >= 0) rest = rest.left(query);
  url.path = rest;
  return url;
}

}  // namespace

const ToastImage* ToastXml::findImage(const QString& placement) const {
  if (!hasBinding) return nullptr;
  // The Windows 8 templates (ToastImageAndText01-04) have one image, shown where appLogoOverride
  // goes.
  if (bindingTemplate.startsWith(QLatin1String("ToastImageAndText"))) {
    return placement == QLatin1String("appLogoOverride") && !images.empty() ? &images.front()
                                                                            : nullptr;
  }
  for (const ToastImage& image : images)
    if (image.placement == placement) return &image;
  return nullptr;
}

const ToastImage* ToastXml::findInlineImage() const {
  if (!hasBinding) return nullptr;
  if (bindingTemplate.startsWith(QLatin1String("ToastImageAndText"))) return nullptr;
  for (const ToastImage& image : images)
    if (image.placement.isEmpty()) return &image;
  return nullptr;
}

std::optional<ToastXml> parseToastXml(const QString& xml) {
  const auto root = parseDocument(xml);
  if (!root) return std::nullopt;
  ToastXml toast;
  toast.launch = root->attr(QStringLiteral("launch"));
  toast.activationType = root->attr(QStringLiteral("activationType"), QStringLiteral("foreground"));
  toast.scenario = root->attr(QStringLiteral("scenario"));
  if (const QString stamp = root->attr(QStringLiteral("displayTimestamp")); !stamp.isEmpty()) {
    QDateTime parsed = QDateTime::fromString(stamp, Qt::ISODateWithMs);
    if (parsed.isValid()) {
      if (parsed.timeSpec() == Qt::LocalTime) parsed.setTimeZone(QTimeZone::UTC);
      toast.displayTimestamp = parsed;
    }
  }
  // Python reads the first <binding> for images and every visual/binding/text for texts.
  bool firstBinding = true;
  for (const Node* visual : root->childrenNamed(QStringLiteral("visual"))) {
    for (const Node* binding : visual->childrenNamed(QStringLiteral("binding"))) {
      if (firstBinding) {
        firstBinding = false;
        toast.hasBinding = true;
        toast.bindingTemplate = binding->attr(QStringLiteral("template"));
        for (const Node* image : binding->childrenNamed(QStringLiteral("image"))) {
          toast.images.push_back({image->attr(QStringLiteral("placement")),
                                  image->attr(QStringLiteral("src")),
                                  image->attr(QStringLiteral("hint-crop"))});
        }
      }
      for (const Node* text : binding->childrenNamed(QStringLiteral("text"))) {
        const QString value = text->text.trimmed();
        if (text->attr(QStringLiteral("placement")) == QLatin1String("attribution")) {
          if (toast.attribution.isEmpty()) toast.attribution = value;
        } else if (!value.isEmpty()) {
          toast.texts << value;
        }
      }
    }
  }
  return toast;
}

std::optional<ToastXml> popToast(std::vector<ToastXml>& toasts, const QStringList& texts) {
  for (auto it = toasts.begin(); it != toasts.end(); ++it) {
    if (it->texts == texts) {
      ToastXml found = std::move(*it);
      toasts.erase(it);
      return found;
    }
  }
  return std::nullopt;
}

QString resolveToastImagePath(const ToastImage* image, const ImagePathEnv& env) {
  if (!image) return {};
  const QString src = image->src.trimmed();
  const ParsedUrl url = parseUrl(src);
  QString relative = QUrl::fromPercentEncoding(url.path.toUtf8());
  while (relative.startsWith(QLatin1Char('/'))) relative.remove(0, 1);

  QString path;
  QString root;
  if (url.scheme == QLatin1String("file") && url.netloc.isEmpty()) {
    // A file on a share would hand the sender's server our credentials.
    path = relative;
  } else if (url.scheme.size() == 1) {
    path = src;  // urlparse takes the drive letter of a plain path for a scheme
  } else if (url.scheme == QLatin1String("ms-appx") && !env.family.isEmpty()) {
    root = env.installFolder ? env.installFolder() : QString();
    if (root.isEmpty()) return {};
    path = root + QLatin1Char('/') + relative;
  } else if (url.scheme == QLatin1String("ms-appdata") && !env.family.isEmpty() &&
             relative.toLower().startsWith(QLatin1String("local/"))) {
    root = env.localAppData + QStringLiteral("/Packages/") + env.family +
           QStringLiteral("/LocalState");
    path = root + QLatin1Char('/') + relative.mid(6);
  } else if (url.scheme == QLatin1String("http") || url.scheme == QLatin1String("https")) {
    // Windows downloaded it to draw the toast, we only read that copy.
    root = env.localAppData + QStringLiteral("/Microsoft/Windows/Notifications/wpnidm");
    path = env.downloadedImage ? env.downloadedImage(src) : QString();
  } else {
    return {};
  }
  if (path.isEmpty() || !QDir::isAbsolutePath(path) || isUnc(path)) return {};
  const QFileInfo info(path);
  const QString resolved = info.canonicalFilePath();  // empty when the file does not exist
  if (resolved.isEmpty() || !info.isFile()) return {};
  if (!root.isEmpty()) {
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (canonicalRoot.isEmpty() || !isUnder(resolved, canonicalRoot)) return {};
  }
  return QDir::toNativeSeparators(resolved);
}

std::vector<NotificationItem> assembleItems(
    const std::vector<RawToast>& raw,
    const std::function<std::vector<ToastXml>(const QString&, int)>& readHistory,
    const std::function<ImagePathEnv(const QString&, const QString&)>& envFor) {
  QHash<QString, int> counts;
  for (const RawToast& toast : raw) ++counts[toast.aumid];
  QHash<QString, std::vector<ToastXml>> history;
  std::vector<NotificationItem> items;
  items.reserve(raw.size());
  for (const RawToast& toast : raw) {
    if (!history.contains(toast.aumid)) {
      history.insert(toast.aumid, toast.aumid.isEmpty() || !readHistory
                                      ? std::vector<ToastXml>{}
                                      : readHistory(toast.aumid, counts.value(toast.aumid)));
    }
    ToastXml xml = popToast(history[toast.aumid], toast.texts).value_or(ToastXml{});
    const ToastImage* logo = xml.findImage(QStringLiteral("appLogoOverride"));
    const ImagePathEnv env = envFor ? envFor(toast.aumid, toast.family) : ImagePathEnv{};

    NotificationItem item;
    item.id = toast.id;
    item.aumid = toast.aumid;
    item.appName = toast.appName;
    item.title = toast.texts.isEmpty() ? QString() : toast.texts.first();
    item.body = toast.texts.size() > 1 ? toast.texts.mid(1).join(QLatin1Char('\n')) : QString();
    item.createdAt = xml.displayTimestamp.value_or(toast.created);
    item.launch = xml.launch;
    item.activationType = xml.activationType;
    item.scenario = xml.scenario;
    item.attribution = xml.attribution;
    item.logo = resolveToastImagePath(logo, env);
    item.logoCircle = logo && logo->hintCrop == QLatin1String("circle");
    item.hero = resolveToastImagePath(xml.findImage(QStringLiteral("hero")), env);
    item.image = resolveToastImagePath(xml.findInlineImage(), env);
    items.push_back(std::move(item));
  }
  return items;
}

}  // namespace waku::notifications
