# clock: date and time, calendar popup

Current design of the capability. QML only: no C++, no service, no thread.

| File | Content |
|---|---|
| `qml/Clock.qml` | The `Clock` widget: a `Pill` with the formatted time, a hover tooltip with the full date, and a click that toggles the calendar popup. |
| `qml/Calendar.qml` | The month grid: six weeks starting on the locale's first weekday, the selected day and month stepping. Also used standalone inside the popup. |

## Behaviour

- `format` and `tooltipFormat` are Qt date-time formats (`Qt.formatDateTime`).
- The text redraws on the next minute boundary, or the next second boundary when `format` contains
  an `s`. The timer is re-armed after each tick, a few ms past the boundary, so the redraw never
  lands just before it.
- `calendar: false` removes the popup and the hover highlight (`interactive` follows `calendar`).
- The popup is the host's `PopupWindow`; the calendar opens on the current month, the selected day
  starts at today and follows a click and the month stepping.
- The popup shows the ISO 8601 week number of the selected day, as `QDate::weekNumber`.

## Dependencies

`wabar` QML types only (`Pill`, `PopupWindow`, `Theme`). No feature dependency.

## Tests

`src/features/clock/tests/test_clock.cpp` runs `ClockWidget.*`, `CalendarWidget.*` and `CalendarPopupTest.*` in a real
bar on the offscreen platform, with the Harness and helpers of `host/testing/` (rule S2 lets the tests
of every feature include them).
