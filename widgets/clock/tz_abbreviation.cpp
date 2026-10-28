#include "widgets/clock/tz_abbreviation.h"

#include <QTimeZone>
#include <cstdlib>
#include <cstring>

namespace yasb::widgets {
namespace {

struct ZoneAbbreviation {
  const char* id;
  const char* standard;
  const char* daylight;  // same as standard when the zone has no daylight time
};

// Letter abbreviations from tzdata. Zones whose tzdata abbreviation is numeric are left out and
// take the computed "+hh[mm]" form.
constexpr ZoneAbbreviation kZones[] = {
    {"UTC", "UTC", "UTC"},
    {"Etc/UTC", "UTC", "UTC"},
    {"Etc/GMT", "GMT", "GMT"},
    {"GMT", "GMT", "GMT"},
    {"America/New_York", "EST", "EDT"},
    {"America/Toronto", "EST", "EDT"},
    {"America/Detroit", "EST", "EDT"},
    {"America/Chicago", "CST", "CDT"},
    {"America/Winnipeg", "CST", "CDT"},
    {"America/Mexico_City", "CST", "CST"},
    {"America/Denver", "MST", "MDT"},
    {"America/Edmonton", "MST", "MDT"},
    {"America/Boise", "MST", "MDT"},
    {"America/Phoenix", "MST", "MST"},
    {"America/Los_Angeles", "PST", "PDT"},
    {"America/Vancouver", "PST", "PDT"},
    {"America/Anchorage", "AKST", "AKDT"},
    {"America/Halifax", "AST", "ADT"},
    {"America/St_Johns", "NST", "NDT"},
    {"America/Puerto_Rico", "AST", "AST"},
    {"Pacific/Honolulu", "HST", "HST"},
    {"Europe/London", "GMT", "BST"},
    {"Europe/Dublin", "GMT", "IST"},
    {"Europe/Lisbon", "WET", "WEST"},
    {"Atlantic/Canary", "WET", "WEST"},
    {"Europe/Paris", "CET", "CEST"},
    {"Europe/Berlin", "CET", "CEST"},
    {"Europe/Madrid", "CET", "CEST"},
    {"Europe/Rome", "CET", "CEST"},
    {"Europe/Amsterdam", "CET", "CEST"},
    {"Europe/Brussels", "CET", "CEST"},
    {"Europe/Luxembourg", "CET", "CEST"},
    {"Europe/Vienna", "CET", "CEST"},
    {"Europe/Zurich", "CET", "CEST"},
    {"Europe/Stockholm", "CET", "CEST"},
    {"Europe/Oslo", "CET", "CEST"},
    {"Europe/Copenhagen", "CET", "CEST"},
    {"Europe/Prague", "CET", "CEST"},
    {"Europe/Warsaw", "CET", "CEST"},
    {"Europe/Budapest", "CET", "CEST"},
    {"Europe/Belgrade", "CET", "CEST"},
    {"Europe/Zagreb", "CET", "CEST"},
    {"Europe/Monaco", "CET", "CEST"},
    {"Europe/Malta", "CET", "CEST"},
    {"Africa/Ceuta", "CET", "CEST"},
    {"Europe/Athens", "EET", "EEST"},
    {"Europe/Helsinki", "EET", "EEST"},
    {"Europe/Kiev", "EET", "EEST"},
    {"Europe/Kyiv", "EET", "EEST"},
    {"Europe/Bucharest", "EET", "EEST"},
    {"Europe/Sofia", "EET", "EEST"},
    {"Europe/Riga", "EET", "EEST"},
    {"Europe/Tallinn", "EET", "EEST"},
    {"Europe/Vilnius", "EET", "EEST"},
    {"Africa/Cairo", "EET", "EEST"},
    {"Europe/Moscow", "MSK", "MSK"},
    {"Africa/Johannesburg", "SAST", "SAST"},
    {"Africa/Lagos", "WAT", "WAT"},
    {"Africa/Nairobi", "EAT", "EAT"},
    {"Asia/Jerusalem", "IST", "IDT"},
    {"Asia/Kolkata", "IST", "IST"},
    {"Asia/Calcutta", "IST", "IST"},
    {"Asia/Karachi", "PKT", "PKT"},
    {"Asia/Tokyo", "JST", "JST"},
    {"Asia/Seoul", "KST", "KST"},
    {"Asia/Shanghai", "CST", "CST"},
    {"Asia/Taipei", "CST", "CST"},
    {"Asia/Hong_Kong", "HKT", "HKT"},
    {"Asia/Manila", "PST", "PST"},
    {"Asia/Jakarta", "WIB", "WIB"},
    {"Australia/Sydney", "AEST", "AEDT"},
    {"Australia/Melbourne", "AEST", "AEDT"},
    {"Australia/Hobart", "AEST", "AEDT"},
    {"Australia/Brisbane", "AEST", "AEST"},
    {"Australia/Adelaide", "ACST", "ACDT"},
    {"Australia/Darwin", "ACST", "ACST"},
    {"Australia/Perth", "AWST", "AWST"},
    {"Pacific/Auckland", "NZST", "NZDT"},
};

// tzdata numeric form: "+04", "-03", "+0530".
QString numericAbbreviation(int offsetSeconds) {
  const QChar sign = offsetSeconds < 0 ? QLatin1Char('-') : QLatin1Char('+');
  const int total = std::abs(offsetSeconds);
  const int hours = total / 3600;
  const int minutes = (total % 3600) / 60;
  QString out = sign + QStringLiteral("%1").arg(hours, 2, 10, QLatin1Char('0'));
  if (minutes != 0) out += QStringLiteral("%1").arg(minutes, 2, 10, QLatin1Char('0'));
  return out;
}

}  // namespace

QString pythonZoneAbbreviation(const QByteArray& zoneId, const QDateTime& utc) {
  const QTimeZone zone(zoneId);
  if (!zone.isValid()) return QString();
  for (const ZoneAbbreviation& entry : kZones) {
    if (zoneId == entry.id) {
      const bool dst = zone.isDaylightTime(utc);
      return QString::fromLatin1(dst ? entry.daylight : entry.standard);
    }
  }
  const QString qt = zone.abbreviation(utc);
  const bool numeric = qt.startsWith(QLatin1String("UTC")) || qt.startsWith(QLatin1String("GMT")) ||
                       qt.isEmpty() || qt.at(0) == QLatin1Char('+') || qt.at(0) == QLatin1Char('-');
  // A plain "UTC" / "GMT" from Qt is a real name; only offset forms ("UTC+04:00") are replaced.
  if (qt == QLatin1String("UTC") || qt == QLatin1String("GMT")) return qt;
  return numeric ? numericAbbreviation(zone.offsetFromUtc(utc)) : qt;
}

}  // namespace yasb::widgets
