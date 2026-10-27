#pragma once

#include <QString>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace yasb::widgets {

// Raised where Python would raise KeyError/IndexError/ValueError from str.format.
class FormatError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Subset of Python str.format: {name}, {a[b][0]}, {a.b}, {{ }}, !s/!r, and specs like >5, .1f, d.
// `kwargs` is a JSON object; missing keys throw FormatError.
std::string pyFormat(const std::string& templ, const nlohmann::json& kwargs);

// Python round(x): half-to-even, returned as an integer.
long long pyRound(double value);

// Python f"{x:.Nf}": exact decimal expansion rounded half-to-even (8.25 -> "8.2").
std::string formatFixed(double value, int precision);

// humanize.naturalsize(bytes, binary=True, gnu=True, format=fmt), e.g. "12.3G", "512B".
std::string naturalSizeGnu(double bytes, int precision);

// humanize.naturaldelta(timedelta(seconds=n)) for non-negative n: "2 hours", "a minute", "3 days".
std::string naturalDelta(long long seconds);

// str(timedelta(seconds=n)): "2:03:00", "3 days, 0:00:05".
std::string timedeltaStr(long long seconds);

// One piece of a label template after the <span>icon</span> split.
struct LabelPart {
  QString text;
  QString cssClass;
  bool isIcon = false;
};

// Mirrors BaseWidget.build_widget_label's re.split + strip + class extraction.
// Plain-text parts get "label" (or "label alt").
std::vector<LabelPart> splitLabelParts(const QString& content, bool isAlt = false);

// True when a raw part (already stripped) is a <span ...>...</span> icon.
bool isSpanPart(const QString& part);

// Sequential str.replace in order, as the memory and battery widgets do.
QString replacePlaceholders(QString text, const std::vector<std::pair<QString, QString>>& pairs);

}  // namespace yasb::widgets
