#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace yasb::widgets {

// re.split("(<span.*?>.*?</span>)", content), then drop empty and whitespace-only parts and strip
// the rest. Span parts keep their tags (the Wi-Fi and Bluetooth widgets put the formatted part,
// tags included, into the QLabel).
inline QStringList rawLabelParts(const QString& content) {
  static const QRegularExpression split(QStringLiteral("(<span.*?>.*?</span>)"));
  QStringList pieces;
  qsizetype last = 0;
  auto it = split.globalMatch(content);
  while (it.hasNext()) {
    const auto match = it.next();
    pieces << content.mid(last, match.capturedStart() - last) << match.captured(1);
    last = match.capturedEnd();
  }
  pieces << content.mid(last);
  QStringList parts;
  for (const QString& piece : pieces) {
    const QString stripped = piece.trimmed();
    if (!stripped.isEmpty()) parts << stripped;
  }
  return parts;
}

// re.sub("<span.*?>|</span>", "", text).strip() is empty.
inline bool isBlankAfterSpans(const QString& text) {
  static const QRegularExpression tags(QStringLiteral("<span.*?>|</span>"));
  QString copy = text;
  return copy.remove(tags).trimmed().isEmpty();
}

}  // namespace yasb::widgets
