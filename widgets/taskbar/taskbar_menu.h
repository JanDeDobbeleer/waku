#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <vector>

namespace yasb::widgets {

struct TerminalProfile {
  QString name;
  QString guid;
};

// get_windows_terminal_profiles: the visible profiles (at most 20) of Windows Terminal's
// settings.json under `localAppData`. The id tells stable, preview and portable installs apart.
// Empty when the file is missing or is not plain JSON.
std::vector<TerminalProfile> windowsTerminalProfiles(const QString& identifier,
                                                     const QString& localAppData);

// Where the context menu opens: centred on the button, 6 px outside the bar (below a top bar, above
// a bottom bar).
QPoint contextMenuPosition(const QRect& barGeometry, const QPoint& buttonCenter,
                           const QSize& menuSize, bool barOnTop);

// humanize.naturalsize(value, binary=True, format="%.2f"): "1 Byte", "12 Bytes", "1.50 KiB".
QString naturalSizeBinary(qint64 bytes);

}  // namespace yasb::widgets
