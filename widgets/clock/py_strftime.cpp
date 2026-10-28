#include "widgets/clock/py_strftime.h"

#include <crtdbg.h>
#include <locale.h>

#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace yasb::widgets {
namespace {

void __cdecl ignoreInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned,
                                    uintptr_t) {}

// Turns a UCRT invalid-parameter abort into errno == EINVAL for the current thread only. The debug
// CRT also raises an assertion dialog first; that report is muted for the duration.
class ScopedIgnoreInvalidParameter {
 public:
  ScopedIgnoreInvalidParameter()
      : previous_(_set_thread_local_invalid_parameter_handler(ignoreInvalidParameter)) {
#ifdef _DEBUG
    previousAssertMode_ = _CrtSetReportMode(_CRT_ASSERT, 0);
#endif
  }
  ~ScopedIgnoreInvalidParameter() {
    _set_thread_local_invalid_parameter_handler(previous_);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT, previousAssertMode_);
#endif
  }
  ScopedIgnoreInvalidParameter(const ScopedIgnoreInvalidParameter&) = delete;
  ScopedIgnoreInvalidParameter& operator=(const ScopedIgnoreInvalidParameter&) = delete;

 private:
  _invalid_parameter_handler previous_;
#ifdef _DEBUG
  int previousAssertMode_ = 0;
#endif
};

QString twoDigits(int value) { return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0')); }

}  // namespace

QString formatUtcOffset(int offsetSeconds, int microseconds, bool colons) {
  const QChar sign = offsetSeconds < 0 ? QLatin1Char('-') : QLatin1Char('+');
  int total = std::abs(offsetSeconds);
  const int hours = total / 3600;
  const int minutes = (total % 3600) / 60;
  const int seconds = total % 60;
  const QString sep = colons ? QStringLiteral(":") : QString();
  QString out = sign + twoDigits(hours) + sep + twoDigits(minutes);
  if (seconds != 0 || microseconds != 0) {
    out += sep + twoDigits(seconds);
    if (microseconds != 0) {
      out += QLatin1Char('.') + QStringLiteral("%1").arg(microseconds, 6, 10, QLatin1Char('0'));
    }
  }
  return out;
}

PyStrftime::PyStrftime(const QString& localeName) {
  if (!localeName.isEmpty()) {
    locale_ = _create_locale(LC_TIME, localeName.toLocal8Bit().constData());
  }
  if (!locale_) locale_ = _create_locale(LC_TIME, "C");
}

PyStrftime::~PyStrftime() {
  if (locale_) _free_locale(static_cast<_locale_t>(locale_));
}

std::optional<QString> PyStrftime::format(const QString& format, const StrftimeInput& input) const {
  if (!input.when.isValid()) return std::nullopt;

  // wrap_strftime: expand the directives the C library lacks (%z %:z %Z %f).
  QString expanded;
  expanded.reserve(format.size() + 16);
  const qsizetype n = format.size();
  for (qsizetype i = 0; i < n; ++i) {
    const QChar ch = format.at(i);
    if (ch != QLatin1Char('%')) {
      expanded += ch;
      continue;
    }
    if (i + 1 >= n) {
      expanded += ch;
      break;
    }
    const QChar next = format.at(++i);
    if (next == QLatin1Char('z')) {
      expanded += formatUtcOffset(input.when.offsetFromUtc(), 0, false);
    } else if (next == QLatin1Char(':') && i + 1 < n && format.at(i + 1) == QLatin1Char('z')) {
      ++i;
      expanded += formatUtcOffset(input.when.offsetFromUtc(), 0, true);
    } else if (next == QLatin1Char('Z')) {
      QString name = input.tzName;
      name.replace(QLatin1Char('%'), QStringLiteral("%%"));
      expanded += name;
    } else if (next == QLatin1Char('f')) {
      expanded += QStringLiteral("%1").arg(input.microsecond, 6, 10, QLatin1Char('0'));
    } else {
      expanded += ch;
      expanded += next;
    }
  }
  if (expanded.isEmpty()) return QString();

  const QDate date = input.when.date();
  const QTime time = input.when.time();
  std::tm tm{};
  tm.tm_year = date.year() - 1900;
  tm.tm_mon = date.month() - 1;
  tm.tm_mday = date.day();
  tm.tm_hour = time.hour();
  tm.tm_min = time.minute();
  tm.tm_sec = time.second();
  tm.tm_wday = date.dayOfWeek() % 7;
  tm.tm_yday = static_cast<int>(date.dayOfYear()) - 1;
  tm.tm_isdst = input.when.isDaylightTime() ? 1 : 0;

  const std::wstring wformat = expanded.toStdWString();
  ScopedIgnoreInvalidParameter guard;
  size_t capacity = 1024;
  const size_t limit = 256 * wformat.size();
  for (;;) {
    std::vector<wchar_t> buffer(capacity);
    errno = 0;
    const size_t written = _wcsftime_l(buffer.data(), buffer.size(), wformat.c_str(), &tm,
                                       static_cast<_locale_t>(locale_));
    if (written > 0) return QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(written));
    if (errno == EINVAL) return std::nullopt;
    if (capacity >= limit) return QString();
    capacity *= 2;
  }
}

}  // namespace yasb::widgets
