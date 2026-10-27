#include "core/widgets/label_format.h"

#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <regex>

namespace yasb::widgets {
namespace {

std::string reprDouble(double v) {
  char buf[64];
  const auto r = std::to_chars(buf, buf + sizeof(buf), v);
  std::string s(buf, r.ptr);
  if (s.find_first_of(".en") == std::string::npos) {
    s += ".0";
  }
  return s;
}

std::string reprValue(const nlohmann::json& v) {
  if (v.is_string()) return "'" + v.get<std::string>() + "'";
  if (v.is_object()) {
    std::string s = "{";
    bool first = true;
    for (auto it = v.begin(); it != v.end(); ++it) {
      if (!first) s += ", ";
      first = false;
      s += "'" + it.key() + "': " + reprValue(it.value());
    }
    return s + "}";
  }
  if (v.is_array()) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) {
      if (i) s += ", ";
      s += reprValue(v[i]);
    }
    return s + "]";
  }
  if (v.is_null()) return "None";
  if (v.is_boolean()) return v.get<bool>() ? "True" : "False";
  if (v.is_number_integer()) return std::to_string(v.get<long long>());
  return reprDouble(v.get<double>());
}

std::string toText(const nlohmann::json& v) {
  if (v.is_string()) return v.get<std::string>();
  return reprValue(v);
}

std::string applySpec(const nlohmann::json& v, const std::string& spec) {
  if (spec.empty()) return toText(v);
  static const std::regex re(R"(^(?:(.)?([<>^]))?([0-9]+)?(?:\.([0-9]+))?([fds%])?$)");
  std::smatch m;
  if (!std::regex_match(spec, m, re)) {
    throw FormatError("unsupported format spec: " + spec);
  }
  const char fill = m[1].matched ? m[1].str()[0] : ' ';
  const std::string align = m[2].matched ? m[2].str() : "";
  const int width = m[3].matched ? std::stoi(m[3].str()) : 0;
  const bool hasPrec = m[4].matched;
  const int prec = hasPrec ? std::stoi(m[4].str()) : 0;
  const std::string type = m[5].matched ? m[5].str() : "";

  std::string body;
  bool numeric = v.is_number();
  if (type == "f" || type == "%") {
    if (!numeric) throw FormatError("format code 'f' requires a number");
    const double d = v.get<double>();
    body = type == "%" ? formatFixed(d * 100.0, hasPrec ? prec : 6) + "%"
                       : formatFixed(d, hasPrec ? prec : 6);
  } else if (type == "d") {
    if (!v.is_number_integer()) throw FormatError("format code 'd' requires an integer");
    body = std::to_string(v.get<long long>());
  } else if (v.is_number_float() && hasPrec) {
    body = formatFixed(v.get<double>(), prec);  // not exact for 'g', but unused by the widgets
  } else {
    body = toText(v);
    if (hasPrec && v.is_string() && static_cast<int>(body.size()) > prec) body.resize(prec);
  }
  if (static_cast<int>(body.size()) >= width) return body;
  const size_t pad = static_cast<size_t>(width) - body.size();
  const std::string a = align.empty() ? (numeric ? ">" : "<") : align;
  if (a == "<") return body + std::string(pad, fill);
  if (a == ">") return std::string(pad, fill) + body;
  return std::string(pad / 2, fill) + body + std::string(pad - pad / 2, fill);
}

const nlohmann::json& lookup(const nlohmann::json& root, const std::string& field) {
  size_t i = 0;
  while (i < field.size() && field[i] != '[' && field[i] != '.') ++i;
  const std::string head = field.substr(0, i);
  if (head.empty()) throw FormatError("positional fields are not supported");
  if (!root.is_object() || !root.contains(head)) throw FormatError("KeyError: " + head);
  const nlohmann::json* cur = &root[head];
  while (i < field.size()) {
    if (field[i] == '[') {
      const size_t end = field.find(']', i);
      if (end == std::string::npos) throw FormatError("expected ']' in field");
      const std::string key = field.substr(i + 1, end - i - 1);
      const bool numericKey =
          !key.empty() && key.find_first_not_of("0123456789") == std::string::npos;
      if (numericKey && cur->is_array()) {
        const size_t idx = std::stoul(key);
        if (idx >= cur->size()) throw FormatError("IndexError: " + key);
        cur = &(*cur)[idx];
      } else {
        if (!cur->is_object() || !cur->contains(key)) throw FormatError("KeyError: " + key);
        cur = &(*cur)[key];
      }
      i = end + 1;
    } else {
      size_t j = i + 1;
      while (j < field.size() && field[j] != '[' && field[j] != '.') ++j;
      const std::string attr = field.substr(i + 1, j - i - 1);
      if (!cur->is_object() || !cur->contains(attr)) throw FormatError("AttributeError: " + attr);
      cur = &(*cur)[attr];
      i = j;
    }
  }
  return *cur;
}

}  // namespace

std::string pyFormat(const std::string& templ, const nlohmann::json& kwargs) {
  std::string out;
  for (size_t i = 0; i < templ.size(); ++i) {
    const char c = templ[i];
    if (c == '{') {
      if (i + 1 < templ.size() && templ[i + 1] == '{') {
        out += '{';
        ++i;
        continue;
      }
      int depth = 1;
      size_t j = i + 1;
      while (j < templ.size() && depth > 0) {
        if (templ[j] == '{')
          ++depth;
        else if (templ[j] == '}')
          --depth;
        ++j;
      }
      if (depth != 0) throw FormatError("expected '}' before end of string");
      std::string body = templ.substr(i + 1, j - i - 2);
      std::string spec;
      std::string conv;
      // The spec starts at the first ':' outside [] brackets.
      int bracket = 0;
      for (size_t k = 0; k < body.size(); ++k) {
        if (body[k] == '[')
          ++bracket;
        else if (body[k] == ']')
          --bracket;
        else if (body[k] == ':' && bracket == 0) {
          spec = body.substr(k + 1);
          body.resize(k);
          break;
        }
      }
      const size_t bang = body.find('!');
      if (bang != std::string::npos) {
        conv = body.substr(bang + 1);
        body.resize(bang);
      }
      const nlohmann::json& value = lookup(kwargs, body);
      if (conv == "r" && value.is_string()) {
        out += "'" + value.get<std::string>() + "'";
      } else if (conv == "s" || conv == "r" || conv == "a" || conv.empty()) {
        out += applySpec(conv == "s" ? nlohmann::json(toText(value)) : value, spec);
      } else {
        throw FormatError("unknown conversion: " + conv);
      }
      i = j - 1;
    } else if (c == '}') {
      if (i + 1 < templ.size() && templ[i + 1] == '}') {
        out += '}';
        ++i;
        continue;
      }
      throw FormatError("Single '}' encountered in format string");
    } else {
      out += c;
    }
  }
  return out;
}

long long pyRound(double value) { return static_cast<long long>(std::nearbyint(value)); }

std::string formatFixed(double value, int precision) {
  char buf[512];
  const auto r = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::fixed, precision);
  return std::string(buf, r.ptr);
}

std::string naturalSizeGnu(double bytes, int precision) {
  static const char* kSuffix[] = {"K", "M", "G", "T", "P", "E", "Z", "Y"};
  const double base = 1024.0;
  const double absBytes = std::fabs(bytes);
  if (absBytes < base) return std::to_string(static_cast<long long>(bytes)) + "B";
  int exp = static_cast<int>(std::min(std::log(absBytes) / std::log(base), 8.0));
  return formatFixed(bytes / std::pow(base, exp), precision) + kSuffix[exp - 1];
}

bool isSpanPart(const QString& part) {
  return part.contains(QLatin1String("<span")) && part.contains(QLatin1String("</span>"));
}

std::vector<LabelPart> splitLabelParts(const QString& content, bool isAlt) {
  static const QRegularExpression splitRe(QStringLiteral("(<span.*?>.*?</span>)"));
  static const QRegularExpression classRe(QStringLiteral("class=([\"'])([^\"']+?)\\1"));
  static const QRegularExpression stripRe(QStringLiteral("<span.*?>|</span>"));

  std::vector<LabelPart> parts;
  qsizetype last = 0;
  QStringList pieces;
  auto it = splitRe.globalMatch(content);
  while (it.hasNext()) {
    const auto m = it.next();
    pieces << content.mid(last, m.capturedStart() - last) << m.captured(1);
    last = m.capturedEnd();
  }
  pieces << content.mid(last);

  for (QString piece : pieces) {
    if (piece.isEmpty()) continue;
    piece = piece.trimmed();
    if (piece.isEmpty()) continue;
    LabelPart p;
    if (isSpanPart(piece)) {
      p.isIcon = true;
      const auto cm = classRe.match(piece);
      p.cssClass = cm.hasMatch() ? cm.captured(2) : QStringLiteral("icon");
      p.text = piece.remove(stripRe).trimmed();
    } else {
      p.cssClass = isAlt ? QStringLiteral("label alt") : QStringLiteral("label");
      p.text = piece;
    }
    parts.push_back(std::move(p));
  }
  return parts;
}

QString replacePlaceholders(QString text, const std::vector<std::pair<QString, QString>>& pairs) {
  for (const auto& [key, value] : pairs) text.replace(key, value);
  return text;
}

std::string naturalDelta(long long total) {
  const long long days365 = total / 86400;
  const long long secs = total % 86400;
  long long years = days365 / 365;
  const long long days = days365 % 365;
  const long long months = pyRound(static_cast<double>(days) / 30.5);
  auto plural = [](long long n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
  };
  if (years == 0 && days < 1) {
    if (secs == 0) return "a moment";
    if (secs == 1) return "a second";
    if (secs < 60) return plural(secs, "second", "seconds");
    if (secs < 3600) {
      const long long m = pyRound(static_cast<double>(secs) / 60);
      if (m == 1) return "a minute";
      if (m == 60) return "an hour";
      return plural(m, "minute", "minutes");
    }
    const long long h = pyRound(static_cast<double>(secs) / 3600);
    if (h == 1) return "an hour";
    if (h == 24) return "a day";
    return plural(h, "hour", "hours");
  }
  if (years == 0) {
    if (days == 1) return "a day";
    if (months == 0) return plural(days, "day", "days");
    if (months == 1) return "a month";
    if (months == 12) return "a year";
    return plural(months, "month", "months");
  }
  if (years == 1) {
    if (months == 0 && days == 0) return "a year";
    if (months == 0) return "1 year, " + plural(days, "day", "days");
    if (months == 1) return "1 year, 1 month";
    if (months != 12) return "1 year, " + std::to_string(months) + " months";
    ++years;
  }
  return plural(years, "year", "years");
}

std::string timedeltaStr(long long seconds) {
  const long long days = seconds / 86400;
  const long long rem = seconds % 86400;
  char buf[32];
  std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", rem / 3600, rem % 3600 / 60, rem % 60);
  if (days == 0) return buf;
  return std::to_string(days) + (days == 1 ? " day, " : " days, ") + buf;
}

}  // namespace yasb::widgets
