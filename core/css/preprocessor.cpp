#include "core/css/preprocessor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QRegularExpression>

namespace yasb::css {

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcPreprocessor, "yasb.css.preprocessor")

namespace {

using Re = QRegularExpression;
constexpr auto kUnicode = Re::UseUnicodePropertiesOption;

template <class Fn>
QString replaceAll(const Re& re, const QString& text, Fn&& fn) {
  QString out;
  qsizetype last = 0;
  auto it = re.globalMatch(text);
  while (it.hasNext()) {
    const auto m = it.next();
    out += QStringView(text).mid(last, m.capturedStart() - last);
    out += fn(m);
    last = m.capturedEnd();
  }
  out += QStringView(text).mid(last);
  return out;
}

QString removeComments(const QString& css) {
  QString out;
  qsizetype i = 0;
  while (true) {
    const qsizetype open = css.indexOf(u"/*", i);
    if (open < 0) break;
    const qsizetype close = css.indexOf(u"*/", open + 2);
    if (close < 0) break;
    out += QStringView(css).mid(i, open - i);
    i = close + 2;
  }
  out += QStringView(css).mid(i);

  QString result;
  qsizetype pos = 0;
  while (true) {
    const qsizetype slashes = out.indexOf(u"//", pos);
    if (slashes < 0) break;
    result += QStringView(out).mid(pos, slashes - pos);
    const qsizetype eol = out.indexOf(u'\n', slashes);
    if (eol < 0) {
      pos = out.size();
      break;
    }
    pos = eol;
  }
  result += QStringView(out).mid(pos);
  return result;
}

bool isWinAbs(const QString& path) {
  QString p = path;
  p.replace(u'/', u'\\');
  if (p.startsWith(u"\\\\")) return true;
  return p.size() >= 3 && p[0].isLetter() && p[1] == u':' && p[2] == u'\\';
}

QString pyJoin(const QString& base, const QString& path) {
  if (isWinAbs(path)) return path;
  if (path.startsWith(u'/') || path.startsWith(u'\\')) {
    const QString drive = (base.size() >= 2 && base[1] == u':') ? base.left(2) : QString();
    return drive + path;
  }
  if (base.isEmpty()) return path;
  if (base.endsWith(u'/') || base.endsWith(u'\\')) return base + path;
  return base + u'\\' + path;
}

QString pyNormpath(const QString& path) {
  const QString cleaned = QDir::cleanPath(path);
  return cleaned.isEmpty() ? QStringLiteral(".") : QDir::toNativeSeparators(cleaned);
}

QString pyDirname(const QString& path) {
  const qsizetype i = qMax(path.lastIndexOf(u'/'), path.lastIndexOf(u'\\'));
  if (i < 0) return {};
  QString head = path.left(i);
  bool allSeps = !head.isEmpty();
  for (const QChar c : head) allSeps = allSeps && (c == u'/' || c == u'\\');
  if (!head.isEmpty() && !allSeps) {
    while (head.endsWith(u'/') || head.endsWith(u'\\')) head.chop(1);
  }
  return head;
}

QString pyRepr(const QString& s) {
  QString r = s;
  r.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  return u'\'' + r + u'\'';
}

QString stripQuotes(const QString& s) {
  qsizetype b = 0;
  qsizetype e = s.size();
  while (b < e && (s[b] == u'\'' || s[b] == u'"')) ++b;
  while (e > b && (s[e - 1] == u'\'' || s[e - 1] == u'"')) --e;
  return s.mid(b, e - b);
}

QString extractAndReplaceVariables(const QString& cssIn) {
  QList<QString> names;
  QList<QString> values;
  QHash<QString, qsizetype> index;

  static const Re rootRe(QStringLiteral(R"(:root\s*\{([^}]*)\})"), kUnicode);
  static const Re declRe(QStringLiteral(R"(--([\w-]+)\s*:\s*([^;]+);)"), kUnicode);
  static const Re varRe(QStringLiteral(R"(var\((--[\w-]+)\))"), kUnicode);

  QString css = replaceAll(rootRe, cssIn, [&](const QRegularExpressionMatch& m) {
    auto it = declRe.globalMatch(m.captured(1));
    while (it.hasNext()) {
      const auto dm = it.next();
      const QString name = QStringLiteral("--") + dm.captured(1).trimmed();
      const QString value = dm.captured(2).trimmed();
      const auto found = index.constFind(name);
      if (found == index.constEnd()) {
        index.insert(name, names.size());
        names.append(name);
        values.append(value);
      } else {
        values[*found] = value;
      }
    }
    return QString();
  });

  constexpr int kMaxIterations = 10;
  for (int pass = 0; pass < kMaxIterations; ++pass) {
    bool changed = false;
    for (qsizetype k = 0; k < names.size(); ++k) {
      const QString current = values[k];
      const QString next = replaceAll(varRe, current, [&](const QRegularExpressionMatch& m) {
        const auto found = index.constFind(m.captured(1).trimmed());
        if (found != index.constEnd()) {
          changed = true;
          return values[*found];
        }
        return m.captured(0);
      });
      if (next != current) {
        values[k] = next;
        changed = true;
      }
    }
    if (!changed) break;
  }

  css = replaceAll(varRe, css, [&](const QRegularExpressionMatch& m) {
    const auto found = index.constFind(m.captured(1).trimmed());
    return found != index.constEnd() ? values[*found] : m.captured(0);
  });

  static const Re hexAlpha(QStringLiteral(R"(#([0-9a-fA-F]{8})\b)"), kUnicode);
  return replaceAll(hexAlpha, css, [](const QRegularExpressionMatch& m) {
    const QString hex = m.captured(1);
    return u'#' + hex.mid(6, 2) + hex.left(6);
  });
}

QString normalizeQproperty(const QString& css) {
  static const Re re(QStringLiteral(R"((?<=[{;\s])-qproperty-(?=[\w-]+\s*:))"), kUnicode);
  return replaceAll(re, css,
                    [](const QRegularExpressionMatch&) { return QStringLiteral("qproperty-"); });
}

}  // namespace

CssPreprocessor::CssPreprocessor(const QString& cssPath)
    : cssPath_(cssPath), basePath_(pyDirname(cssPath)) {
  cssContent_ = readCssFile(cssPath);
}

QString CssPreprocessor::process() {
  if (cssContent_.isEmpty()) return {};
  importedFiles_.clear();
  QString css = removeComments(cssContent_);
  css = processImports(css);
  css = removeComments(css);
  css = extractAndReplaceVariables(css);
  css = normalizeQproperty(css);
  return resolveUrls(css);
}

void CssPreprocessor::warn(const QString& message) {
  log_.append({QStringLiteral("WARNING"), message});
  qCWarning(lcPreprocessor).noquote() << message;
}

void CssPreprocessor::error(const QString& message) {
  log_.append({QStringLiteral("ERROR"), message});
  qCWarning(lcPreprocessor).noquote() << message;
}

QString CssPreprocessor::readCssFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    const QString reason = QFileInfo::exists(path)
                               ? QStringLiteral("[Errno 13] Permission denied: ")
                               : QStringLiteral("[Errno 2] No such file or directory: ");
    error(QStringLiteral("CSSProcessor Error '%1': %2%3").arg(path, reason, pyRepr(path)));
    return {};
  }
  QString text = QString::fromUtf8(file.readAll());
  // Python's text mode translates every line ending to \n.
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(u'\r', u'\n');
  return text;
}

QString CssPreprocessor::processImports(const QString& css) {
  static const Re importRe(
      QStringLiteral(R"(@import\s+(?:url\((["']?)([^)]+?)\1\)|(["'])([^\n]+?)\3)\s*;)"),
      Re::CaseInsensitiveOption | kUnicode);
  return replaceAll(importRe, css, [&](const QRegularExpressionMatch& m) {
    QString path = m.captured(2);
    if (path.isEmpty()) path = m.captured(4);
    const QString importPath = stripQuotes(path);
    const QString full = pyNormpath(pyJoin(basePath_, importPath));
    if (importedFiles_.contains(full)) {
      warn(QStringLiteral("Circular import detected: %1").arg(full));
      return QString();
    }
    importedFiles_.insert(full);
    const QString imported = readCssFile(full);
    return imported.isEmpty() ? QString() : processImports(imported);
  });
}

QString CssPreprocessor::resolveUrls(const QString& css) {
  static const Re urlRe(QStringLiteral(R"(url\((["']?)([^)]+?)\1\))"), kUnicode);
  return replaceAll(urlRe, css, [&](const QRegularExpressionMatch& m) {
    const QString quote = m.captured(1);
    const QString path = m.captured(2);
    for (const QString& prefix : {u"data:"_s, u"file:"_s, u"http:"_s, u"https:"_s, u"qrc:"_s}) {
      if (path.startsWith(prefix)) return m.captured(0);
    }
    if (isWinAbs(path)) return m.captured(0);
    QString abs = pyNormpath(pyJoin(basePath_, path));
    if (!QFileInfo(abs).isFile()) {
      warn(QStringLiteral("CSSProcessor: url() references missing file: %1").arg(abs));
    }
    abs.replace(u'\\', u'/');
    return QStringLiteral("url(%1%2%1)").arg(quote, abs);
  });
}

}  // namespace yasb::css
