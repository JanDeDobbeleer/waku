#pragma once

#include <QDateTime>
#include <QString>
#include <optional>

namespace yasb::widgets {

// What datetime.strftime needs besides the wall-clock fields: sub-second digits for %f and the
// zone name for %Z (the UTC offset comes from `when`).
struct StrftimeInput {
  QDateTime when;
  int microsecond = 0;
  QString tzName;
};

// Port of CPython 3.14 datetime.strftime on Windows: %z, %:z, %Z and %f are expanded first, the
// rest goes to the UCRT _wcsftime_l, so the set of accepted directives (%#d, %Ez, ...) and the
// rejected ones (%-d, %k, a trailing %) match Python. Returns std::nullopt where Python raises
// ValueError("Invalid format string").
//
// The C runtime locale for LC_TIME is held per formatter, never the process-wide one. An empty
// or unusable locale name behaves as Python does with a failed setlocale: the "C" locale.
class PyStrftime {
 public:
  explicit PyStrftime(const QString& localeName = QString());
  ~PyStrftime();
  PyStrftime(const PyStrftime&) = delete;
  PyStrftime& operator=(const PyStrftime&) = delete;

  std::optional<QString> format(const QString& format, const StrftimeInput& input) const;

 private:
  void* locale_ = nullptr;  // _locale_t
};

// "+HHMM[SS[.ffffff]]" or with colons, exactly as datetime's %z / %:z.
QString formatUtcOffset(int offsetSeconds, int microseconds, bool colons);

}  // namespace yasb::widgets
