#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace yasb::widgets {

// The abbreviation Python's zoneinfo (tzdata) reports for `%Z`: "IST", "CET", "EDT", or the
// numeric form "+04" / "+0530" for zones without a letter abbreviation. Qt's own abbreviation on
// Windows is often "GMT+5:30", so a table of the common IANA zones comes first and a computed
// numeric form replaces any "UTC+h" or "GMT+h" Qt result. `utc` is the instant being formatted.
QString pythonZoneAbbreviation(const QByteArray& zoneId, const QDateTime& utc);

}  // namespace yasb::widgets
