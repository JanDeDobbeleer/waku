#include "core/css/rules.h"

#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace yasb::css {

using namespace Qt::StringLiterals;
using Re = QRegularExpression;

namespace {

constexpr auto kUnicode = Re::UseUnicodePropertiesOption;

const QSet<QString>& effectProps() {
  static const QSet<QString> s{u"opacity"_s, u"box-shadow"_s};
  return s;
}

const QSet<QString>& borderRadiusProps() {
  static const QSet<QString> s{u"border-radius"_s, u"border-top-left-radius"_s,
                               u"border-top-right-radius"_s, u"border-bottom-right-radius"_s,
                               u"border-bottom-left-radius"_s};
  return s;
}

const QSet<QString>& numericProps() {
  static const QSet<QString> s{
      u"max-height"_s, u"max-width"_s, u"min-height"_s, u"min-width"_s, u"width"_s, u"height"_s,
      u"border-width"_s, u"border-top-width"_s, u"border-right-width"_s, u"border-bottom-width"_s,
      u"border-left-width"_s, u"border-radius"_s, u"border-top-left-radius"_s,
      u"border-top-right-radius"_s, u"border-bottom-left-radius"_s, u"border-bottom-right-radius"_s,
      u"margin"_s, u"margin-top"_s, u"margin-right"_s, u"margin-bottom"_s, u"margin-left"_s,
      u"padding"_s, u"padding-top"_s, u"padding-right"_s, u"padding-bottom"_s, u"padding-left"_s,
      u"font-size"_s, u"font-weight"_s, u"letter-spacing"_s, u"word-spacing"_s, u"spacing"_s,
      u"bottom"_s, u"left"_s, u"right"_s, u"top"_s};
  return s;
}

const QHash<QString, QStringList>& shorthandSides() {
  static const QHash<QString, QStringList> h{
      {u"padding"_s, {u"padding-top"_s, u"padding-right"_s, u"padding-bottom"_s, u"padding-left"_s}},
      {u"margin"_s, {u"margin-top"_s, u"margin-right"_s, u"margin-bottom"_s, u"margin-left"_s}},
      {u"border-width"_s,
       {u"border-top-width"_s, u"border-right-width"_s, u"border-bottom-width"_s,
        u"border-left-width"_s}},
      {u"border-color"_s,
       {u"border-top-color"_s, u"border-right-color"_s, u"border-bottom-color"_s,
        u"border-left-color"_s}},
      {u"border-radius"_s,
       {u"border-top-left-radius"_s, u"border-top-right-radius"_s, u"border-bottom-right-radius"_s,
        u"border-bottom-left-radius"_s}}};
  return h;
}

const QSet<QString>& animationPseudos() {
  static const QSet<QString> s{u":pressed"_s, u":hover"_s, u":focus"_s,
                               u":checked"_s, u":clicked"_s, u":active"_s};
  return s;
}

QString normalizeProp(const QString& name) {
  if (name == u"background") return u"background-color"_s;
  if (name == u"text-shadow") return u"box-shadow"_s;
  return name;
}

bool isAnimatable(const QString& prop) {
  return prop == u"color" || prop.endsWith(u"-color") || effectProps().contains(prop) ||
         numericProps().contains(prop);
}

// ---- Minimal CSS scanner ----------------------------------------------------------------------
// Mirrors the tokenizer behaviours that change where a declaration or rule ends: strings,
// comments, and `url(` (a malformed unquoted url swallows everything up to the next `)`).

bool isIdentChar(QChar c) {
  return c.isLetterOrNumber() || c == u'_' || c == u'-' || c.unicode() >= 0x80;
}

bool isIdentStart(QChar c) { return c.isLetter() || c == u'_' || c.unicode() >= 0x80; }

enum class UnitKind { Char, Comment, String, Url };

struct Unit {
  qsizetype end;
  UnitKind kind;
  bool closed = true;  // strings: terminated by the matching quote
};

qsizetype skipString(const QString& s, qsizetype i, bool* closed) {
  const QChar quote = s[i];
  qsizetype j = i + 1;
  while (j < s.size()) {
    const QChar c = s[j];
    if (c == quote) {
      *closed = true;
      return j + 1;
    }
    if (c == u'\\') {
      j += 2;
      continue;
    }
    if (c == u'\n') break;
    ++j;
  }
  *closed = false;
  return qMin(j, s.size());
}

bool isUrlOpen(const QString& s, qsizetype i) {
  if (i + 4 > s.size() || s.mid(i, 4).toLower() != u"url(") return false;
  return i == 0 || !isIdentChar(s[i - 1]);
}

// Returns -1 when the url holds a quoted string (a plain function token to the tokenizer).
qsizetype skipUrl(const QString& s, qsizetype i, QString* value, bool* bad) {
  qsizetype j = i + 4;
  while (j < s.size() && s[j].isSpace()) ++j;
  if (j < s.size() && (s[j] == u'"' || s[j] == u'\'')) return -1;
  *bad = false;
  value->clear();
  auto consumeBad = [&](qsizetype from) {
    while (from < s.size() && s[from] != u')') from += (s[from] == u'\\') ? 2 : 1;
    return qMin(from + 1, s.size());
  };
  while (j < s.size()) {
    const QChar c = s[j];
    if (c == u')') return j + 1;
    if (c.isSpace()) {
      while (j < s.size() && s[j].isSpace()) ++j;
      if (j >= s.size() || s[j] == u')') return qMin(j + 1, s.size());
      *bad = true;
      return consumeBad(j);
    }
    if (c == u'"' || c == u'\'' || c == u'(' || c.category() == QChar::Other_Control) {
      *bad = true;
      return consumeBad(j);
    }
    if (c == u'\\') {
      if (j + 1 >= s.size() || s[j + 1] == u'\n') {
        *bad = true;
        return consumeBad(j);
      }
      value->append(s[j + 1]);
      j += 2;
      continue;
    }
    value->append(c);
    ++j;
  }
  return s.size();
}

Unit nextUnit(const QString& s, qsizetype i) {
  const QChar c = s[i];
  if (c == u'/' && i + 1 < s.size() && s[i + 1] == u'*') {
    const qsizetype close = s.indexOf(u"*/", i + 2);
    return {close < 0 ? s.size() : close + 2, UnitKind::Comment};
  }
  if (c == u'"' || c == u'\'') {
    bool closed = false;
    const qsizetype end = skipString(s, i, &closed);
    return {end, UnitKind::String, closed};
  }
  if ((c == u'u' || c == u'U') && isUrlOpen(s, i)) {
    QString v;
    bool bad = false;
    const qsizetype end = skipUrl(s, i, &v, &bad);
    if (end >= 0) return {end, UnitKind::Url};
  }
  return {i + 1, UnitKind::Char};
}

QChar closerFor(QChar open) {
  if (open == u'{') return u'}';
  if (open == u'(') return u')';
  return u']';
}

struct BlockSpan {
  qsizetype contentEnd;  // index of the closing brace, or text size when unterminated
  qsizetype next;
};

BlockSpan scanBlock(const QString& s, qsizetype open) {
  QList<QChar> stack{closerFor(s[open])};
  qsizetype i = open + 1;
  while (i < s.size()) {
    const Unit u = nextUnit(s, i);
    if (u.kind == UnitKind::Char) {
      const QChar c = s[i];
      if (c == u'{' || c == u'(' || c == u'[') {
        stack.append(closerFor(c));
      } else if ((c == u'}' || c == u')' || c == u']') && c == stack.back()) {
        stack.removeLast();
        if (stack.isEmpty()) return {i, i + 1};
      }
    }
    i = u.end;
  }
  return {s.size(), s.size()};
}

QString requoteString(const QString& raw, bool closed) {
  if (!closed) return raw;
  const QString inner = raw.mid(1, raw.size() - 2);
  QString value;
  for (qsizetype i = 0; i < inner.size(); ++i) {
    const QChar c = inner[i];
    if (c != u'\\') {
      value.append(c);
      continue;
    }
    if (++i >= inner.size()) break;
    const QChar n = inner[i];
    if (n == u'\n') continue;
    auto hexVal = [](QChar h) { return h.isDigit() || (h.toLower() >= u'a' && h.toLower() <= u'f'); };
    if (hexVal(n)) {
      qsizetype k = i;
      while (k < inner.size() && k - i < 6 && hexVal(inner[k])) ++k;
      const char32_t cp = inner.mid(i, k - i).toUInt(nullptr, 16);
      if (k < inner.size() && inner[k].isSpace()) ++k;
      i = k - 1;
      value.append(cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)
                       ? QString(QChar(0xFFFD))
                       : QString::fromUcs4(&cp, 1));
      continue;
    }
    value.append(n);
  }
  QString out = u"\""_s;
  for (const QChar c : value) {
    if (c == u'\\') out += u"\\\\"_s;
    else if (c == u'"') out += u"\\\""_s;
    else if (c == u'\n') out += u"\\A "_s;
    else if (c == u'\r') out += u"\\D "_s;
    else if (c == u'\f') out += u"\\C "_s;
    else out.append(c);
  }
  return out + u'"';
}

QString serializeUrl(const QString& value, bool bad) {
  if (bad) return u"url([bad url])"_s;
  QString out = u"url("_s;
  for (const QChar c : value) {
    if (c == u'\\' || c == u'"' || c == u'\'' || c == u'(' || c == u')' || c == u' ') {
      out.append(u'\\');
      out.append(c);
    } else if (c.unicode() < 0x20 || c.unicode() == 0x7f) {
      out += QStringLiteral("\\%1 ").arg(static_cast<int>(c.unicode()), 0, 16);
    } else {
      out.append(c);
    }
  }
  return out + u')';
}

// tinycss2.serialize() of a token run: source text, except strings are re-quoted with "
// and url() contents are normalised.
QString serializeRange(const QString& s) {
  QString out;
  qsizetype i = 0;
  while (i < s.size()) {
    const Unit u = nextUnit(s, i);
    switch (u.kind) {
      case UnitKind::String:
        out += requoteString(s.mid(i, u.end - i), u.closed);
        break;
      case UnitKind::Url: {
        QString v;
        bool bad = false;
        skipUrl(s, i, &v, &bad);
        out += serializeUrl(v, bad);
        break;
      }
      default:
        out += QStringView(s).mid(i, u.end - i);
        break;
    }
    i = u.end;
  }
  return out;
}

// ---- Component tokens (values) -----------------------------------------------------------------

struct Tok {
  enum Type { Ident, Number, Dimension, Percentage, Hash, String, Url, Function, Block, Literal };
  Type type = Literal;
  QString text;
  double number = 0;
  QString unit;
  QString name;  // functions: name without the parenthesis
};

QList<Tok> tokenize(const QString& v) {
  static const Re numRe(QStringLiteral(R"([+-]?(?:\d*\.\d+|\d+)(?:[eE][+-]?\d+)?)"),
                        Re::DontCaptureOption);
  QList<Tok> out;
  qsizetype i = 0;
  const qsizetype n = v.size();
  auto identEnd = [&](qsizetype from) {
    while (from < n) {
      if (v[from] == u'\\' && from + 1 < n) {
        from += 2;
      } else if (isIdentChar(v[from])) {
        ++from;
      } else {
        break;
      }
    }
    return from;
  };
  auto startsIdent = [&](qsizetype p) {
    if (p >= n) return false;
    if (isIdentStart(v[p]) || v[p] == u'\\') return true;
    return v[p] == u'-' && p + 1 < n && (isIdentStart(v[p + 1]) || v[p + 1] == u'-' || v[p + 1] == u'\\');
  };
  while (i < n) {
    const QChar c = v[i];
    if (c.isSpace()) {
      ++i;
      continue;
    }
    const Unit u = nextUnit(v, i);
    if (u.kind == UnitKind::Comment) {
      i = u.end;
      continue;
    }
    Tok t;
    if (u.kind == UnitKind::String) {
      t.type = Tok::String;
      t.text = v.mid(i, u.end - i);
      i = u.end;
    } else if (u.kind == UnitKind::Url) {
      t.type = Tok::Url;
      t.text = v.mid(i, u.end - i);
      i = u.end;
    } else {
      const auto m = numRe.match(v, i, Re::NormalMatch, Re::AnchorAtOffsetMatchOption);
      if (m.hasMatch() && (c.isDigit() || c == u'.' || c == u'+' || c == u'-')) {
        t.number = m.captured(0).toDouble();
        qsizetype e = m.capturedEnd();
        t.type = Tok::Number;
        if (startsIdent(e)) {
          const qsizetype ue = identEnd(e);
          t.unit = v.mid(e, ue - e);
          t.type = Tok::Dimension;
          e = ue;
        } else if (e < n && v[e] == u'%') {
          t.type = Tok::Percentage;
          ++e;
        }
        t.text = v.mid(i, e - i);
        i = e;
      } else if (c == u'#') {
        qsizetype e = identEnd(i + 1);
        t.type = Tok::Hash;
        t.text = v.mid(i, e - i);
        i = e;
      } else if (startsIdent(i)) {
        qsizetype e = identEnd(i);
        const qsizetype identStart = i;
        t.name = v.mid(i, e - i);
        if (e < n && v[e] == u'(') {
          t.type = Tok::Function;
          i = scanBlock(v, e).next;
        } else {
          t.type = Tok::Ident;
          i = e;
        }
        t.text = v.mid(identStart, i - identStart);
      } else if (c == u'(' || c == u'[' || c == u'{') {
        const BlockSpan span = scanBlock(v, i);
        t.type = Tok::Block;
        t.text = v.mid(i, span.next - i);
        i = span.next;
      } else {
        t.type = Tok::Literal;
        t.text = QString(c);
        ++i;
      }
    }
    out.append(t);
  }
  return out;
}

QList<QList<Tok>> splitByComma(const QList<Tok>& tokens) {
  QList<QList<Tok>> parts;
  QList<Tok> current;
  for (const Tok& t : tokens) {
    if (t.type == Tok::Literal && t.text == u",") {
      parts.append(current);
      current.clear();
    } else {
      current.append(t);
    }
  }
  parts.append(current);
  return parts;
}

// ---- Shorthands --------------------------------------------------------------------------------

QStringList splitComponents(const QString& value) {
  QStringList out;
  for (const Tok& t : tokenize(value)) out.append(t.text.trimmed());
  return out;
}

QList<Declaration> expandBorder(const QString& value) {
  static const QSet<QString> styles{u"none"_s,   u"hidden"_s, u"dotted"_s, u"dashed"_s, u"solid"_s,
                                    u"double"_s, u"groove"_s, u"ridge"_s,  u"inset"_s,  u"outset"_s};
  static const QSet<QString> widths{u"thin"_s, u"medium"_s, u"thick"_s};
  QList<Declaration> result;
  auto put = [&](const QString& k, const QString& v) {
    for (auto& d : result) {
      if (d.first == k) {
        d.second = v;
        return;
      }
    }
    result.append({k, v});
  };
  for (const QString& token : splitComponents(value)) {
    if (styles.contains(token)) put(u"border-style"_s, token);
    else if (widths.contains(token) || (!token.isEmpty() && token[0].isDigit())) put(u"border-width"_s, token);
    else put(u"border-color"_s, token);
  }
  return result;
}

void putDecl(QList<Declaration>& list, const QString& k, const QString& v) {
  for (auto& d : list) {
    if (d.first == k) {
      d.second = v;
      return;
    }
  }
  list.append({k, v});
}

QList<Declaration> expandShorthand(const QString& prop, const QString& value) {
  if (prop == u"border") {
    QList<Declaration> result;
    for (const auto& [subProp, subVal] : expandBorder(value)) {
      for (const auto& d : expandShorthand(subProp, subVal)) putDecl(result, d.first, d.second);
    }
    return result;
  }
  const auto it = shorthandSides().constFind(prop);
  if (it == shorthandSides().constEnd()) return {{prop, value}};
  const QStringList parts = splitComponents(value);
  QStringList expanded;
  switch (parts.size()) {
    case 0: break;
    case 1: expanded = {parts[0], parts[0], parts[0], parts[0]}; break;
    case 2: expanded = {parts[0], parts[1], parts[0], parts[1]}; break;
    case 3: expanded = {parts[0], parts[1], parts[2], parts[1]}; break;
    default: expanded = parts.mid(0, 4); break;
  }
  QList<Declaration> result;
  for (qsizetype i = 0; i < expanded.size(); ++i) result.append({(*it)[i], expanded[i]});
  return result;
}

QStringList transitionLonghands(const QString& prop) {
  if (prop == u"border") {
    return shorthandSides().value(u"border-width"_s) + shorthandSides().value(u"border-color"_s);
  }
  const auto it = shorthandSides().constFind(prop);
  return it != shorthandSides().constEnd() ? *it : QStringList{prop};
}

QList<Declaration> staticDeclarations(const QString& prop, const QString& value,
                                      const QSet<QString>& animated) {
  const QList<Declaration> expanded = expandShorthand(prop, value);
  QList<Declaration> remaining;
  for (const auto& d : expanded) {
    const bool strip = isAnimatable(d.first) && (animated.contains(u"all"_s) || animated.contains(d.first));
    if (!strip) remaining.append(d);
  }
  if (remaining.size() == expanded.size()) return {{prop, value}};
  return remaining;
}

// ---- Transition declarations -------------------------------------------------------------------

bool isNone(const QList<Tok>& tokens) {
  return tokens.size() == 1 && tokens[0].type == Tok::Ident && tokens[0].text.toLower() == u"none";
}

int timeMs(const Tok& t) { return static_cast<int>(t.number * (t.unit == u"s" ? 1000 : 1)); }

bool isTime(const Tok& t) { return t.type == Tok::Dimension && (t.unit == u"ms" || t.unit == u"s"); }

bool isEasingFn(const Tok& t) {
  const QString n = t.name.toLower();
  return t.type == Tok::Function && (n == u"cubic-bezier" || n == u"steps");
}

QStringList parseTransitionPropertyList(const QList<Tok>& tokens) {
  QStringList result;
  for (const auto& seg : splitByComma(tokens)) {
    if (seg.isEmpty() || seg[0].type != Tok::Ident) continue;
    if (seg[0].text.toLower() == u"none") return {};
    result.append(seg[0].text);
  }
  return result;
}

QList<int> parseTimeList(const QList<Tok>& tokens) {
  QList<int> result;
  for (const auto& seg : splitByComma(tokens)) {
    if (seg.isEmpty()) continue;
    const Tok& t = seg[0];
    if (isTime(t)) result.append(timeMs(t));
    else if (t.type == Tok::Number && t.number == 0) result.append(0);
  }
  return result;
}

QStringList parseEasingList(const QList<Tok>& tokens) {
  QStringList result;
  for (const auto& seg : splitByComma(tokens)) {
    if (seg.isEmpty()) continue;
    const Tok& t = seg[0];
    if (t.type == Tok::Ident || isEasingFn(t)) result.append(t.text);
  }
  return result;
}

struct Segment {
  QString prop;
  int duration;
  QString easing;
  int delay;
};

std::optional<Segment> parseTransitionSegment(const QList<Tok>& tokens) {
  if (tokens.size() < 2) return std::nullopt;
  if (tokens[0].type != Tok::Ident || !isTime(tokens[1])) return std::nullopt;
  Segment s{tokens[0].text, timeMs(tokens[1]), u"ease"_s, 0};
  for (qsizetype i = 2; i < tokens.size(); ++i) {
    const Tok& t = tokens[i];
    if (isTime(t)) s.delay = timeMs(t);
    else if (t.type == Tok::Ident && t.text != u"normal" && t.text != u"allow-discrete") s.easing = t.text;
    else if (isEasingFn(t)) s.easing = t.text;
  }
  return s;
}

template <class T>
T pick(const std::optional<QList<T>>& list, qsizetype i, const T& fallback) {
  if (!list || list->isEmpty()) return fallback;
  return (*list)[i % list->size()];
}

QList<TransitionSpec> combineLonghands(const std::optional<QStringList>& props,
                                       const std::optional<QList<int>>& durations,
                                       const std::optional<QStringList>& easings,
                                       const std::optional<QList<int>>& delays) {
  QList<TransitionSpec> result;
  if (!props || props->isEmpty() || !durations || durations->isEmpty()) return result;
  const std::optional<QList<QString>> easingList = easings;
  for (qsizetype i = 0; i < props->size(); ++i) {
    const int dur = pick<int>(durations, i, 0);
    const QString easing = pick<QString>(easingList, i, u"ease"_s);
    const int delay = pick<int>(delays, i, 0);
    for (const QString& lh : transitionLonghands(normalizeProp((*props)[i]))) {
      result.append({lh, dur, easing, delay});
    }
  }
  return result;
}

// ---- Selectors ---------------------------------------------------------------------------------

struct SplitSelector {
  QString base;
  QStringList pseudos;
};

SplitSelector splitSelector(const QString& selector) {
  static const Re trailing(QStringLiteral(R"(((?:(?<!:):[a-z-]+)+)$)"));
  static const Re one(QStringLiteral(R"(:[a-z-]+)"));
  const auto m = trailing.match(selector);
  if (!m.hasMatch()) return {selector, {}};
  QSet<QString> found;
  auto it = one.globalMatch(m.captured(1));
  while (it.hasNext()) {
    const QString p = it.next().captured(0);
    if (animationPseudos().contains(p)) found.insert(p);
  }
  QStringList sorted(found.begin(), found.end());
  sorted.sort();
  return {selector.left(m.capturedStart()), sorted};
}

std::array<int, 3> computeSpecificity(const QString& base, const QStringList& pseudos) {
  static const Re attrRe(QStringLiteral(R"(\[[^\]]+\])"));
  static const Re idRe(QStringLiteral(R"(#([^.#\s\[:]+))"));
  int a = 0;
  int b = static_cast<int>(pseudos.size());
  int c = 0;
  for (const QString& segment : base.split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts)) {
    b += static_cast<int>(parseAttrConditions(segment).size());
    QString noAttrs = segment;
    noAttrs.remove(attrRe);
    auto it = idRe.globalMatch(noAttrs);
    while (it.hasNext()) {
      it.next();
      ++a;
    }
    QString noId = noAttrs;
    noId.remove(idRe);
    QString noSub = noId.split(u"::"_s).first();
    QString head = noSub.split(u':').first();
    const QStringList parts = head.split(u'.');
    const QString tag = parts[0].trimmed();
    if (!tag.isEmpty() && tag != u"*") ++c;
    for (qsizetype i = 1; i < parts.size(); ++i) {
      if (!parts[i].trimmed().isEmpty()) ++b;
    }
  }
  return {a, b, c};
}

// ---- Stylesheet parse --------------------------------------------------------------------------

struct RawDecl {
  QString name;  // lower-cased
  QString value;  // serialised, trimmed, `!important` removed
};

struct RawRule {
  QString selector;
  QList<RawDecl> decls;
  int line = 0;
};

bool validIdent(const QString& name) {
  static const Re re(QStringLiteral(R"(^(?:--|-?(?:[A-Za-z_\x{80}-\x{10FFFF}]|\\.))(?:[\w\x{80}-\x{10FFFF}-]|\\.)*$)"),
                     kUnicode);
  return re.match(name).hasMatch();
}

QList<RawDecl> parseDeclarations(const QString& s) {
  QList<RawDecl> out;
  QList<QChar> stack;
  qsizetype itemStart = 0;
  bool nested = false;
  auto finish = [&](qsizetype end) {
    const QString item = s.mid(itemStart, end - itemStart);
    itemStart = end + 1;
    if (nested) {
      nested = false;
      return;
    }
    const QString trimmed = item.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(u'@')) return;
    qsizetype colon = -1;
    for (qsizetype i = 0; i < item.size();) {
      const Unit u = nextUnit(item, i);
      if (u.kind == UnitKind::Char && item[i] == u':') {
        colon = i;
        break;
      }
      i = u.end;
    }
    if (colon < 0) return;
    const QString name = item.left(colon).trimmed();
    if (!validIdent(name)) return;
    static const Re important(QStringLiteral(R"(!\s*important\s*$)"), Re::CaseInsensitiveOption);
    QString raw = item.mid(colon + 1);
    const auto m = important.match(raw);
    if (m.hasMatch()) raw.truncate(m.capturedStart());
    out.append({name.toLower(), serializeRange(raw).trimmed()});
  };
  qsizetype i = 0;
  while (i < s.size()) {
    const Unit u = nextUnit(s, i);
    if (u.kind == UnitKind::Char) {
      const QChar c = s[i];
      if (c == u';' && stack.isEmpty()) {
        finish(i);
      } else if (c == u'{' || c == u'(' || c == u'[') {
        if (c == u'{' && stack.isEmpty()) nested = true;
        stack.append(closerFor(c));
      } else if ((c == u'}' || c == u')' || c == u']') && !stack.isEmpty() && c == stack.back()) {
        stack.removeLast();
        if (stack.isEmpty() && c == u'}' && nested) finish(i + 1);
      }
    }
    i = u.end;
  }
  if (itemStart < s.size() || nested) finish(s.size());
  return out;
}

QStringList splitTopLevelCommas(const QString& s) {
  QStringList parts;
  QList<QChar> stack;
  qsizetype start = 0;
  for (qsizetype i = 0; i < s.size();) {
    const Unit u = nextUnit(s, i);
    if (u.kind == UnitKind::Char) {
      const QChar c = s[i];
      if (c == u',' && stack.isEmpty()) {
        parts.append(s.mid(start, i - start));
        start = i + 1;
      } else if (c == u'{' || c == u'(' || c == u'[') {
        stack.append(closerFor(c));
      } else if ((c == u'}' || c == u')' || c == u']') && !stack.isEmpty() && c == stack.back()) {
        stack.removeLast();
      }
    }
    i = u.end;
  }
  parts.append(s.mid(start));
  return parts;
}

QList<RawRule> parseStylesheet(const QString& s) {
  QList<qsizetype> newlines;
  for (qsizetype i = 0; i < s.size(); ++i) {
    if (s[i] == u'\n') newlines.append(i);
  }
  auto lineOf = [&](qsizetype pos) {
    return static_cast<int>(std::lower_bound(newlines.begin(), newlines.end(), pos) - newlines.begin()) + 1;
  };

  QList<RawRule> rules;
  qsizetype i = 0;
  const qsizetype n = s.size();
  while (i < n) {
    if (s[i].isSpace()) {
      ++i;
      continue;
    }
    const Unit lead = nextUnit(s, i);
    if (lead.kind == UnitKind::Comment) {
      i = lead.end;
      continue;
    }
    const qsizetype start = i;
    const bool atRule = s[i] == u'@';
    QList<QChar> stack;
    qsizetype blockOpen = -1;
    qsizetype j = i;
    qsizetype after = n;
    while (j < n) {
      const Unit u = nextUnit(s, j);
      if (u.kind == UnitKind::Char) {
        const QChar c = s[j];
        if (c == u'{' && stack.isEmpty()) {
          blockOpen = j;
          break;
        }
        if (atRule && c == u';' && stack.isEmpty()) {
          after = j + 1;
          break;
        }
        if (c == u'{' || c == u'(' || c == u'[') {
          stack.append(closerFor(c));
        } else if ((c == u')' || c == u']' || c == u'}') && !stack.isEmpty() && c == stack.back()) {
          stack.removeLast();
        }
      }
      j = u.end;
    }
    if (blockOpen < 0) {
      i = after;
      if (after == n) break;
      continue;
    }
    const BlockSpan span = scanBlock(s, blockOpen);
    i = span.next;
    if (atRule) continue;
    const QString prelude = s.mid(start, blockOpen - start);
    const QString content = s.mid(blockOpen + 1, span.contentEnd - blockOpen - 1);
    const QList<RawDecl> decls = parseDeclarations(content);
    for (const QString& piece : splitTopLevelCommas(prelude)) {
      rules.append({serializeRange(piece).trimmed(), decls, lineOf(start)});
    }
  }
  return rules;
}

}  // namespace

bool StyleRule::has(const QString& name) const {
  return std::any_of(properties.begin(), properties.end(), [&](const Declaration& d) { return d.first == name; });
}

QString StyleRule::value(const QString& name) const {
  for (const auto& d : properties) {
    if (d.first == name) return d.second;
  }
  return {};
}

// ---- Selector helpers --------------------------------------------------------------------------

QList<AttrCondition> parseAttrConditions(const QString& segment) {
  static const Re attrRe(QStringLiteral(R"(\[([^\]]+)\])"));
  static const Re content(
      QStringLiteral(R"re(^\s*([A-Za-z_][\w\-.]*)(?:\s*([~|^$*!]?=)\s*(?:"([^"]*)"|'([^']*)'|([^\s'"\]]+))\s*)?$)re"),
      kUnicode);
  QList<AttrCondition> out;
  auto it = attrRe.globalMatch(segment);
  while (it.hasNext()) {
    const auto parsed = content.match(it.next().captured(1));
    if (!parsed.hasMatch()) continue;
    const QString op = parsed.captured(2);
    if (op.isEmpty()) {
      out.append({parsed.captured(1), u"exists"_s, std::nullopt});
    } else if (op == u"=") {
      QString value = parsed.captured(3);
      if (parsed.capturedStart(3) < 0) value = parsed.capturedStart(4) >= 0 ? parsed.captured(4) : parsed.captured(5);
      out.append({parsed.captured(1), u"="_s, value});
    } else {
      out.append({parsed.captured(1), op, std::nullopt});
    }
  }
  return out;
}

CompiledSegment compileSegment(const QString& segment) {
  static const Re attrRe(QStringLiteral(R"(\[([^\]]+)\])"));
  static const Re idRe(QStringLiteral(R"(#([^.#\s\[:]+))"));
  CompiledSegment c;
  QString stripped = segment;
  stripped.remove(attrRe);
  c.attrs = parseAttrConditions(segment);
  const auto id = idRe.match(stripped);
  if (id.hasMatch()) {
    c.objName = id.captured(1);
    stripped.remove(id.capturedStart(), id.capturedLength());
  }
  const QStringList parts = stripped.split(u'.');
  for (qsizetype i = 1; i < parts.size(); ++i) {
    if (!parts[i].isEmpty()) c.classes.insert(parts[i]);
  }
  if (!parts[0].isEmpty() && parts[0] != u"*") c.tag = parts[0];
  return c;
}

// ---- Gradients ---------------------------------------------------------------------------------

namespace {

QStringList splitArgs(const QString& s) {
  QStringList parts;
  int depth = 0;
  QString current;
  for (const QChar c : s) {
    if (c == u'(') {
      ++depth;
      current.append(c);
    } else if (c == u')') {
      --depth;
      current.append(c);
    } else if (c == u',' && depth == 0) {
      parts.append(current.trimmed());
      current.clear();
    } else {
      current.append(c);
    }
  }
  if (!current.isEmpty()) parts.append(current.trimmed());
  return parts;
}

using Stop = std::pair<std::optional<double>, QString>;

Stop parseStop(const QString& in) {
  static const Re re(QStringLiteral(R"(\s+([\d.]+)(%?)\s*$)"));
  const QString s = in.trimmed();
  const auto m = re.match(s);
  if (m.hasMatch()) {
    const double val = m.captured(1).toDouble();
    return {m.captured(2) == u"%" ? val / 100.0 : val, s.left(m.capturedStart()).trimmed()};
  }
  return {std::nullopt, s};
}

QList<std::pair<double, QString>> fillPositions(const QList<Stop>& raw) {
  const qsizetype n = raw.size();
  QList<std::optional<double>> pos;
  for (const auto& r : raw) pos.append(r.first);
  if (!pos.first()) pos.first() = 0.0;
  if (!pos.last()) pos.last() = 1.0;
  for (qsizetype i = 0; i < n;) {
    if (!pos[i]) {
      qsizetype j = i + 1;
      while (j < n && !pos[j]) ++j;
      const double ps = pos[i - 1].value_or(0.0);
      const double pe = j < n ? pos[j].value_or(1.0) : 1.0;
      const qsizetype count = j - i + 1;
      for (qsizetype k = i; k < j; ++k) pos[k] = ps + (pe - ps) * static_cast<double>(k - i + 1) / static_cast<double>(count);
    }
    ++i;
  }
  QList<std::pair<double, QString>> out;
  for (qsizetype i = 0; i < n; ++i) out.append({pos[i].value_or(0.0), raw[i].second});
  return out;
}

QString g4(double v) { return QString::number(v, 'g', 4); }

QString stopsString(const QStringList& stopArgs) {
  QList<Stop> raw;
  for (const QString& a : stopArgs) raw.append(parseStop(a));
  QStringList parts;
  for (const auto& [p, color] : fillPositions(raw)) parts.append(u"stop:%1 %2"_s.arg(g4(p), color));
  return parts.join(u", "_s);
}

double parsePosValue(const QString& in) {
  static const Re re(QStringLiteral(R"(^(-?[\d.]+)(%?)$)"));
  const auto m = re.match(in.trimmed());
  if (!m.hasMatch()) return 0.5;
  const double v = m.captured(1).toDouble();
  return m.captured(2) == u"%" ? v / 100.0 : v;
}

std::optional<QString> translateLinear(const QString& inner) {
  static const QHash<QString, std::array<double, 4>> dirs{
      {u"to right"_s, {0, 0, 1, 0}},        {u"to left"_s, {1, 0, 0, 0}},
      {u"to bottom"_s, {0, 0, 0, 1}},       {u"to top"_s, {0, 1, 0, 0}},
      {u"to bottom right"_s, {0, 0, 1, 1}}, {u"to right bottom"_s, {0, 0, 1, 1}},
      {u"to bottom left"_s, {1, 0, 0, 1}},  {u"to left bottom"_s, {1, 0, 0, 1}},
      {u"to top right"_s, {0, 1, 1, 0}},    {u"to right top"_s, {0, 1, 1, 0}},
      {u"to top left"_s, {1, 1, 0, 0}},     {u"to left top"_s, {1, 1, 0, 0}}};
  static const Re angleRe(QStringLiteral(R"(^(-?[\d.]+)deg$)"));
  const QStringList args = splitArgs(inner);
  if (args.isEmpty()) return std::nullopt;
  std::optional<std::array<double, 4>> dir;
  const QString d = args[0].trimmed().toLower();
  if (dirs.contains(d)) {
    dir = dirs.value(d);
  } else if (const auto m = angleRe.match(d); m.hasMatch()) {
    const double rad = m.captured(1).toDouble() * std::numbers::pi / 180.0;
    auto r6 = [](double v) { return std::round(v * 1e6) / 1e6; };
    const double x2 = 0.5 + 0.5 * std::sin(rad);
    const double y2 = 0.5 - 0.5 * std::cos(rad);
    dir = std::array<double, 4>{r6(1.0 - x2), r6(1.0 - y2), r6(x2), r6(y2)};
  }
  QStringList stopArgs = dir ? args.mid(1) : args;
  if (!dir) dir = std::array<double, 4>{0, 0, 0, 1};
  if (stopArgs.size() < 2) return std::nullopt;
  return u"qlineargradient(x1:%1, y1:%2, x2:%3, y2:%4, %5)"_s.arg(g4((*dir)[0]), g4((*dir)[1]), g4((*dir)[2]), g4((*dir)[3]), stopsString(stopArgs));
}

std::optional<QString> translateRadial(const QString& inner) {
  static const Re wordRe(QStringLiteral(R"([a-z-]+)"));
  static const Re atRe(QStringLiteral(R"(\bat\s+([\d.]+%?)\s+([\d.]+%?))"), Re::CaseInsensitiveOption);
  static const Re shapeRe(QStringLiteral(R"(\b(circle|ellipse|closest-side|farthest-side|closest-corner|farthest-corner)\b)"),
                          Re::CaseInsensitiveOption);
  static const Re sizeRe(QStringLiteral(R"(([\d.]+)(%?))"));
  static const QSet<QString> keywords{u"circle"_s, u"ellipse"_s, u"at"_s, u"closest-side"_s,
                                      u"farthest-side"_s, u"closest-corner"_s, u"farthest-corner"_s};
  const QStringList args = splitArgs(inner);
  if (args.isEmpty()) return std::nullopt;
  double cx = 0.5, cy = 0.5, radius = 0.5;
  qsizetype stopStart = 0;
  bool descriptor = false;
  auto words = wordRe.globalMatch(args[0].toLower());
  while (words.hasNext()) descriptor = descriptor || keywords.contains(words.next().captured(0));
  if (descriptor) {
    stopStart = 1;
    QString desc = args[0];
    const auto at = atRe.match(desc);
    if (at.hasMatch()) {
      cx = parsePosValue(at.captured(1));
      cy = parsePosValue(at.captured(2));
      desc = desc.left(at.capturedStart());
    }
    desc.remove(shapeRe);
    const auto sm = sizeRe.match(desc.trimmed());
    if (sm.hasMatch()) {
      const double v = sm.captured(1).toDouble();
      radius = sm.captured(2) == u"%" ? v / 100.0 : v;
    }
  }
  const QStringList stopArgs = args.mid(stopStart);
  if (stopArgs.size() < 2) return std::nullopt;
  return u"qradialgradient(cx:%1, cy:%2, radius:%3, fx:%1, fy:%2, %4)"_s.arg(g4(cx), g4(cy), g4(radius), stopsString(stopArgs));
}

std::optional<QString> translateConic(const QString& inner) {
  static const Re atRe(QStringLiteral(R"(\bat\s+([\d.]+%?)\s+([\d.]+%?))"), Re::CaseInsensitiveOption);
  static const Re fromRe(QStringLiteral(R"(\bfrom\s+(-?[\d.]+)deg\b)"), Re::CaseInsensitiveOption);
  const QStringList args = splitArgs(inner);
  if (args.isEmpty()) return std::nullopt;
  double cx = 0.5, cy = 0.5, angle = 0.0;
  qsizetype stopStart = 0;
  const QStringList firstWords = args[0].toLower().split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts);
  if (firstWords.contains(u"from") || firstWords.contains(u"at")) {
    stopStart = 1;
    if (const auto fm = fromRe.match(args[0]); fm.hasMatch()) angle = fm.captured(1).toDouble();
    if (const auto am = atRe.match(args[0]); am.hasMatch()) {
      cx = parsePosValue(am.captured(1));
      cy = parsePosValue(am.captured(2));
    }
  }
  const QStringList stopArgs = args.mid(stopStart);
  if (stopArgs.size() < 2) return std::nullopt;
  return u"qconicalgradient(cx:%1, cy:%2, angle:%3, %4)"_s.arg(g4(cx), g4(cy), g4(angle), stopsString(stopArgs));
}

}  // namespace

QString translateGradients(const QString& value) {
  static const Re gradientRe(QStringLiteral(R"(\b(linear|radial|conic)-gradient\s*\()"), Re::CaseInsensitiveOption);
  QString result = value;
  qsizetype offset = 0;
  auto it = gradientRe.globalMatch(value);
  while (it.hasNext()) {
    const auto m = it.next();
    const QString type = m.captured(1).toLower();
    const qsizetype fnStart = m.capturedStart() + offset;
    const qsizetype parenOpen = m.capturedEnd() - 1 + offset;
    int depth = 0;
    qsizetype parenClose = -1;
    for (qsizetype i = parenOpen; i < result.size(); ++i) {
      if (result[i] == u'(') ++depth;
      else if (result[i] == u')' && --depth == 0) {
        parenClose = i;
        break;
      }
    }
    if (parenClose < 0) continue;
    const QString inner = result.mid(parenOpen + 1, parenClose - parenOpen - 1);
    const auto translated = type == u"linear" ? translateLinear(inner)
                            : type == u"radial" ? translateRadial(inner)
                                                : translateConic(inner);
    if (!translated) continue;
    const qsizetype oldLen = parenClose - fnStart + 1;
    result = result.left(fnStart) + *translated + result.mid(parenClose + 1);
    offset += translated->size() - oldLen;
  }
  return result;
}

// ---- extract_rules -----------------------------------------------------------------------------

ParsedStyleSheet extractRules(const QString& stylesheet) {
  static const Re gradientValue(QStringLiteral(R"(\b(?:q(?:linear|radial|conical)gradient|(?:linear|radial|conic)-gradient)\s*\()"),
                                Re::CaseInsensitiveOption);
  static const Re attrBlock(QStringLiteral(R"(\[[^\]]*\])"));
  static const Re ws(QStringLiteral(R"(\s+)"));

  const QList<RawRule> raw = parseStylesheet(stylesheet);
  QList<StyleRule> rules;

  for (const RawRule& rr : raw) {
    std::optional<QStringList> tProps;
    std::optional<QList<int>> tDurations;
    std::optional<QStringList> tEasings;
    std::optional<QList<int>> tDelays;
    QList<TransitionSpec> transitions;
    bool resets = false;
    bool sawTransition = false;
    QList<Declaration> props;

    for (const RawDecl& decl : rr.decls) {
      const QList<Tok> value = tokenize(decl.value);
      const QString& name = decl.name;
      if (name == u"transition") {
        sawTransition = true;
        if (isNone(value)) {
          transitions.clear();
          resets = true;
          tProps = QStringList{};
          tDurations = QList<int>{0};
          tEasings = QStringList{u"ease"_s};
          tDelays = QList<int>{0};
          continue;
        }
        QList<Segment> entries;
        const auto segments = splitByComma(value);
        for (const auto& seg : segments) {
          if (const auto r = parseTransitionSegment(seg)) entries.append(*r);
        }
        const bool anyNone = std::any_of(entries.begin(), entries.end(),
                                         [](const Segment& e) { return e.prop.toLower() == u"none"; });
        if (anyNone) {
          if (entries.size() == 1 && segments.size() == 1) {
            transitions.clear();
            resets = true;
            tProps = QStringList{};
            tDurations = QList<int>{entries[0].duration};
            tEasings = QStringList{entries[0].easing};
            tDelays = QList<int>{entries[0].delay};
          }
          continue;
        }
        if (!entries.isEmpty()) {
          transitions += combineLonghands(tProps, tDurations, tEasings, tDelays);
          tProps = QStringList{};
          tDurations = QList<int>{};
          tEasings = QStringList{};
          tDelays = QList<int>{};
          for (const Segment& e : entries) {
            tProps->append(e.prop);
            tDurations->append(e.duration);
            tEasings->append(e.easing);
            tDelays->append(e.delay);
          }
        }
      } else if (name == u"transition-property") {
        if (isNone(value)) {
          transitions.clear();
          resets = true;
          tProps = QStringList{};
          continue;
        }
        tProps = parseTransitionPropertyList(value);
      } else if (name == u"transition-duration") {
        tDurations = parseTimeList(value);
      } else if (name == u"transition-timing-function") {
        tEasings = parseEasingList(value);
      } else if (name == u"transition-delay") {
        tDelays = parseTimeList(value);
      } else {
        const QString norm = normalizeProp(name);
        for (const auto& d : expandShorthand(norm, translateGradients(decl.value))) putDecl(props, d.first, d.second);
      }
    }
    transitions += combineLonghands(tProps, tDurations, tEasings, tDelays);

    StyleRule rule;
    rule.selector = rr.selector;
    const SplitSelector split = splitSelector(rr.selector);
    rule.baseSelector = split.base;
    rule.pseudoSet = split.pseudos;
    rule.properties = props;
    rule.subcontrol = split.base.contains(u"::");
    rule.transitions = rule.subcontrol ? QList<TransitionSpec>{} : transitions;
    rule.segments = split.base.split(ws, Qt::SkipEmptyParts);
    rule.hasAttrs = split.base.contains(u'[');
    rule.specificity = computeSpecificity(split.base, split.pseudos);
    rule.order = static_cast<int>(rules.size());
    rule.resetsTransitions = resets && !rule.subcontrol;
    rule.droppedSubcontrolTransition = rule.subcontrol && sawTransition;
    rule.line = rr.line;
    for (const auto& d : rule.properties) {
      rule.hasEffectProps = rule.hasEffectProps || effectProps().contains(d.first);
      rule.hasBorderRadiusProps = rule.hasBorderRadiusProps || borderRadiusProps().contains(d.first);
      rule.hasCursorProp = rule.hasCursorProp || d.first == u"cursor";
    }
    rules.append(rule);
  }

  QHash<QString, QSet<QString>> animatedMap;
  for (const StyleRule& r : rules) {
    if (r.transitions.isEmpty()) continue;
    for (const auto& t : r.transitions) animatedMap[r.baseSelector].insert(t.prop);
  }

  QStringList cleanedParts;
  for (const RawRule& rr : raw) {
    const SplitSelector split = splitSelector(rr.selector);
    const bool hasAttrBlock = rr.selector.contains(u'[');
    QSet<QString> animated = animatedMap.value(split.base);
    if (hasAttrBlock) {
      QString stripped = split.base;
      stripped.remove(attrBlock);
      stripped = stripped.replace(ws, u" "_s).trimmed();
      animated.unite(animatedMap.value(stripped));
    }
    QStringList lines;
    for (const RawDecl& decl : rr.decls) {
      if (decl.name == u"transition" || decl.name.startsWith(u"transition-")) continue;
      const QString pName = normalizeProp(decl.name);
      const QString pVal = translateGradients(decl.value);
      if (pName == u"box-shadow" || pName == u"cursor") continue;
      QList<Declaration> declarations;
      const bool staticGradient = pName == u"background-color" && gradientValue.match(pVal).hasMatch();
      if ((!split.pseudos.isEmpty() || hasAttrBlock) && !staticGradient) {
        declarations = staticDeclarations(pName, pVal, animated);
      } else {
        declarations = {{pName, pVal}};
      }
      for (const auto& d : declarations) lines.append(u"    %1: %2;"_s.arg(d.first, d.second));
    }
    if (!lines.isEmpty()) {
      cleanedParts.append(rr.selector + u" {\n"_s + lines.join(u'\n') + u"\n}"_s);
    } else {
      cleanedParts.append(rr.selector + u" { }"_s);
    }
  }
  return {cleanedParts.join(u"\n\n"_s), rules};
}

}  // namespace yasb::css
