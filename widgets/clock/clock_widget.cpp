#include "widgets/clock/clock_widget.h"

#include <QApplication>
#include <QCalendarWidget>
#include <QCoreApplication>
#include <QDir>
#include <QMenu>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QTableView>
#include <QTextCharFormat>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <ctime>

#include "core/bar/theme_class.h"
#include "core/widgets/backdrop.h"
#include "core/widgets/tooltip.h"
#include "platform/sound.h"
#include "widgets/clock/tz_abbreviation.h"

namespace yasb::widgets {
namespace {

constexpr int kAlarmSoundLoopMs = 16000;

QString localZoneName(bool dst) {
  _tzset();
  char buffer[128] = {};
  size_t size = 0;
  if (_get_tzname(&size, buffer, sizeof(buffer), dst ? 1 : 0) != 0) return {};
  return QString::fromLocal8Bit(buffer);
}

QString zoneDisplay(const std::optional<std::string>& tz) {
  if (!tz) return QStringLiteral("Local time");
  return QString::fromStdString(*tz).replace(QLatin1Char('_'), QLatin1Char(' '));
}

QString twoDigits(int value) { return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0')); }

}  // namespace

ClockInstant systemClockInstant() {
  using namespace std::chrono;
  const auto micros = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
  ClockInstant instant;
  instant.utc = QDateTime::fromMSecsSinceEpoch(micros / 1000, QTimeZone::UTC);
  instant.microsecond = static_cast<int>(micros % 1000000);
  return instant;
}

ClockWidget::ClockWidget(const WidgetContext& context, ClockSharedState* state, InstantFn clock)
    : BaseWidget(QString::fromStdString(context.name), context.eventBus,
                 QStringLiteral("clock-widget ") +
                     QString::fromStdString(
                         std::get<config::options::ClockConfig>(context.typedOptions).class_name),
                 std::get<config::options::ClockConfig>(context.typedOptions).update_interval),
      options_(std::get<config::options::ClockConfig>(context.typedOptions)),
      globalState_(context.globalState),
      state_(state ? *state : ClockSharedState::instance()),
      clock_(clock ? std::move(clock) : InstantFn(systemClockInstant)),
      formatter_(QString::fromStdString(
          std::get<config::options::ClockConfig>(context.typedOptions).locale)),
      label_(QString::fromStdString(options_.label)),
      labelAlt_(QString::fromStdString(options_.label_alt)) {
  // timezones: keep configured ones the platform knows, "local" when none are left.
  if (options_.timezones.empty()) {
    timezoneList_.push_back(std::nullopt);
  } else {
    for (const std::string& tz : options_.timezones) {
      if (QTimeZone::isTimeZoneIdAvailable(QByteArray::fromStdString(tz))) {
        timezoneList_.push_back(tz);
      } else {
        qWarning().noquote() << "Invalid timezone" << QString::fromStdString(tz)
                             << "ignored. Use format like 'America/New_York' or 'Europe/London'";
      }
    }
    if (timezoneList_.empty()) {
      qWarning() << "No valid timezones found, using system local timezone";
      timezoneList_.push_back(std::nullopt);
    }
  }
  timezoneCycle_ = timezoneList_;

  buildWidgetLabel(label_, labelAlt_);

  timerLabel_ = new QLabel(labelContainer());
  timerLabel_->setProperty("class", "label timer");
  timerLabel_->setAlignment(Qt::AlignCenter);
  timerLabel_->hide();
  labelContainer()->layout()->addWidget(timerLabel_);

  registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) {
    toggleAlt();
    render();
  });
  registerCallback(QStringLiteral("update_label"), [this](const QStringList&) { render(); });
  registerCallback(QStringLiteral("next_timezone"), [this](const QStringList&) { nextTimezone(); });
  registerCallback(QStringLiteral("toggle_calendar"),
                   [this](const QStringList&) { showCalendar(); });
  registerCallback(QStringLiteral("context_menu"),
                   [this](const QStringList&) { showContextMenu(); });
  registerCallback(QStringLiteral("timer_tick"), [this](const QStringList&) { state_.tick(); });
  registerCallback(QStringLiteral("toggle_timer"),
                   [this](const QStringList&) { showTimerDialog(); });
  registerCallback(QStringLiteral("toggle_alarm"),
                   [this](const QStringList&) { showAlarmDialog(); });
  setCallbackBinding({QString::fromStdString(options_.callbacks.on_left),
                      QString::fromStdString(options_.callbacks.on_middle),
                      QString::fromStdString(options_.callbacks.on_right),
                      QStringLiteral("timer_tick")});

  state_.registerClient(this);
  nextTimezone();
  render();
}

ClockWidget::~ClockWidget() { state_.unregisterClient(this); }

// Python starts a plain QTimer, so its labels drift against the wall clock. Here the repeating
// timer is re-armed on the next interval boundary (the second for a 1000 ms interval) so a minute
// rolls over on the :00 tick. The first refresh still runs at once.
void ClockWidget::startTicking() {
  startTimer();
  const int interval = options_.update_interval;
  if (interval <= 0 || 60000 % interval != 0) return;
  const qint64 millis = clock_().utc.toMSecsSinceEpoch();
  const int delay = interval - static_cast<int>(millis % interval);
  if (delay == interval) return;
  stopTimer();
  QTimer::singleShot(delay, this, [this] { startTimer(); });
}

void ClockWidget::refreshClock(bool updateTooltipToo) {
  render();
  if (updateTooltipToo) updateTooltip();
}

QLocale ClockWidget::qlocale() const {
  return options_.locale.empty() ? QLocale::system() : QLocale(localeName());
}

ClockWidget::Moment ClockWidget::now() const {
  const ClockInstant instant = clock_();
  Moment moment;
  moment.microsecond = instant.microsecond;
  if (activeTz_) {
    const QTimeZone zone(QByteArray::fromStdString(*activeTz_));
    moment.when = instant.utc.toTimeZone(zone);
    moment.tzName = pythonZoneAbbreviation(QByteArray::fromStdString(*activeTz_), instant.utc);
  } else {
    moment.when = instant.utc.toLocalTime();
    moment.tzName = localZoneName(moment.when.isDaylightTime());
  }
  return moment;
}

QString ClockWidget::iconForHour(int hour) const {
  const auto find = [&](int h) -> const std::string* {
    const auto it = options_.icons.find("clock_" + twoDigits(h).toStdString());
    return it == options_.icons.end() ? nullptr : &it->second;
  };
  const std::string* icon = find(hour);
  if (!icon && hour >= 13 && hour <= 23) icon = find(hour - 12);
  return icon ? QString::fromStdString(*icon) : QString();
}

QString ClockWidget::timerText() const {
  const int total = state_.timerSecondsRemaining();
  const int hours = total / 3600;
  const int minutes = (total % 3600) / 60;
  const int seconds = total % 60;
  if (hours > 0) {
    return QStringLiteral("[%1:%2:%3]")
        .arg(twoDigits(hours), twoDigits(minutes), twoDigits(seconds));
  }
  return QStringLiteral("[%1:%2]").arg(twoDigits(minutes), twoDigits(seconds));
}

void ClockWidget::render() {
  try {
    const bool alt = showAlt();
    const auto& active = alt ? labelsAlt() : labels();
    const auto parts = splitLabelParts(alt ? labelAlt_ : label_, alt);
    const Moment moment = now();
    const int hour = moment.when.time().hour();
    const QString currentHour = twoDigits(hour);
    const QString altClass = alt ? QStringLiteral(" alt") : QString();

    const bool timerOn = state_.timerActive() && state_.timerSecondsRemaining() >= 0;
    if (timerOn) {
      timerLabel_->setText(timerText());
      setLabelClass(timerLabel_, QStringLiteral("label%1 timer").arg(altClass));
      if (!timerVisible_) {
        timerLabel_->show();
        timerVisible_ = true;
      }
    } else if (timerVisible_) {
      timerLabel_->hide();
      timerVisible_ = false;
    }

    const bool snoozed = !state_.snoozed().empty();
    const bool alarms = state_.hasEnabledAlarms();
    const QString alarmIcon = QString::fromStdString(snoozed ? options_.alarm_icons.snooze
                                                             : options_.alarm_icons.enabled);
    static const QRegularExpression formatRe(QStringLiteral("\\{(.*)\\}"));

    for (size_t i = 0; i < parts.size() && i < active.size(); ++i) {
      QLabel* label = active[i];
      const LabelPart& part = parts[i];
      if (part.isIcon) {
        if (part.text == QLatin1String("{icon}")) {
          const QString icon = iconForHour(hour);
          if (label->text() != icon) label->setText(icon);
          setLabelClass(label, QStringLiteral("icon%1 clock_%2").arg(altClass, currentHour));
        } else if (part.text == QLatin1String("{alarm}")) {
          if (snoozed || alarms) {
            label->setText(alarmIcon);
            setLabelClass(label,
                          QStringLiteral("icon%1 alarm%2")
                              .arg(altClass, snoozed ? QStringLiteral(" snooze") : QString()));
            label->setVisible(true);
          } else {
            label->setText(QString());
            label->setVisible(false);
          }
        } else {
          label->setText(part.text);
        }
        continue;
      }

      QString text = part.text;
      const bool hasAlarm = text.contains(QLatin1String("{alarm}")) && (snoozed || alarms);
      if (text.contains(QLatin1String("{icon}")))
        text.replace(QLatin1String("{icon}"), iconForHour(hour));
      if (text.contains(QLatin1String("{alarm}"))) {
        text.replace(QLatin1String("{alarm}"), snoozed || alarms ? alarmIcon : QString());
      }
      QString output = text;
      const auto match = formatRe.match(text);
      if (match.hasMatch()) {
        StrftimeInput input{moment.when, moment.microsecond, moment.tzName};
        if (const auto formatted = formatter_.format(match.captured(1), input)) {
          output = text;
          output.replace(match.captured(0), *formatted);
        }
      }
      label->setText(output);
      setLabelClass(label, hasAlarm
                               ? QStringLiteral("label%1 alarm%2")
                                     .arg(altClass, snoozed ? QStringLiteral(" snooze") : QString())
                               : QStringLiteral("label%1 clock_%2").arg(altClass, currentHour));
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "Clock label update failed:" << error.what();
  }
}

QString ClockWidget::tooltipText() const {
  const Moment moment = now();
  const std::optional<QString> date = formatter_.format(
      QStringLiteral("%A, %d %B %Y"), {moment.when, moment.microsecond, moment.tzName});
  const std::optional<QString> abbr =
      formatter_.format(QStringLiteral("%a"), {moment.when, moment.microsecond, moment.tzName});
  const std::optional<QString> time =
      formatter_.format(QStringLiteral("%H:%M"), {moment.when, moment.microsecond, moment.tzName});
  QString text = QStringLiteral("%1\n\n%2 %3 (%4)")
                     .arg(date.value_or(QString()), abbr.value_or(QString()),
                          time.value_or(QString()), zoneDisplay(activeTz_));
  if (state_.hasEnabledAlarms()) {
    text += QStringLiteral("\n\n") + alarmsTooltip(state_.alarms(), state_.snoozed());
  }
  return text;
}

void ClockWidget::updateTooltip() {
  if (!options_.tooltip) return;
  try {
    setTooltip(this, tooltipText(), globalState_);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Error updating clock tooltip:" << error.what();
  }
}

void ClockWidget::nextTimezone() {
  activeTz_ = timezoneCycle_[cyclePos_ % timezoneCycle_.size()];
  ++cyclePos_;
  if (activeTz_ && !QTimeZone::isTimeZoneIdAvailable(QByteArray::fromStdString(*activeTz_))) {
    qWarning().noquote() << "Error switching to timezone" << QString::fromStdString(*activeTz_);
    activeTz_.reset();
    updateTooltip();
    render();
    return;
  }
  updateTooltip();
  render();
  if (options_.tooltip) showTooltipNow(this);
}

void ClockWidget::setTimezone(const std::optional<std::string>& timezone) {
  timezoneCycle_.clear();
  timezoneCycle_.push_back(timezone);
  for (const auto& tz : timezoneList_) {
    if (tz != timezone) timezoneCycle_.push_back(tz);
  }
  cyclePos_ = 0;
  nextTimezone();
}

PinnablePopup* ClockWidget::newPopup(const QString& cssClass) {
  const auto& calendar = options_.calendar;
  auto* popup = new PinnablePopup(this, globalState_, calendar.blur, calendar.round_corners,
                                  QString::fromStdString(calendar.round_corners_type),
                                  QString::fromStdString(calendar.border_color));
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setProperty("class", cssClass);
  return popup;
}

void ClockWidget::positionPopup(PinnablePopup* popup) {
  const auto& calendar = options_.calendar;
  popup->adjustSize();
  popup->setPosition(QString::fromStdString(calendar.alignment),
                     QString::fromStdString(calendar.direction), calendar.offset_left,
                     calendar.offset_top);
}

void ClockWidget::showCalendar() {
  try {
    if (calendarPopup_) calendarPopup_->deleteLater();
    PinnablePopup* popup = newPopup(QStringLiteral("clock-popup calendar"));
    calendarPopup_ = popup;
    const auto& config = options_.calendar;

    auto* layout = new QHBoxLayout(popup);
    layout->setProperty("class", "calendar-layout");

    auto* dateLayout = new QVBoxLayout;
    dateLayout->setContentsMargins(0, 0, 0, 0);
    dateLayout->setSpacing(0);
    dateLayout->setAlignment(Qt::AlignCenter);

    const Moment moment = now();
    const QDate today = moment.when.date();
    const QLocale locale = qlocale();

    const auto makeLabel = [&](const QString& text, const char* cssClass) {
      auto* label = new QLabel(text);
      label->setProperty("class", cssClass);
      label->setAlignment(Qt::AlignCenter);
      dateLayout->addWidget(label);
      return label;
    };
    dayLabel_ = makeLabel(locale.dayName(today.dayOfWeek()), "day-label");
    monthLabel_ = makeLabel(locale.monthName(today.month()), "month-label");
    yearLabel_ = nullptr;
    if (config.show_years) yearLabel_ = makeLabel(QString::number(today.year()), "year-label");
    dateLabel_ = makeLabel(QString::number(today.day()), "date-label");
    weekLabel_ = nullptr;
    if (config.show_week_numbers) {
      weekLabel_ = makeLabel(QStringLiteral("Week %1").arg(today.weekNumber()), "week-label");
    }
    if (config.show_holidays) {
      // The holidays package has no C++ counterpart: the label exists for styling, empty.
      auto* holiday = makeLabel(QString(), "holiday-label");
      holiday->setWordWrap(true);
    }
    layout->addLayout(dateLayout);

    auto* calendar = new QCalendarWidget(this);
    calendar_ = calendar;
    calendar->setGridVisible(false);
    calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar->setNavigationBarVisible(false);
    calendar->setAutoFillBackground(false);
    const QTextCharFormat weekday = calendar->weekdayTextFormat(Qt::Monday);
    for (int day = Qt::Monday; day <= Qt::Sunday; ++day) {
      calendar->setWeekdayTextFormat(static_cast<Qt::DayOfWeek>(day), weekday);
    }
    if (auto* table = calendar->findChild<QTableView*>()) {
      table->setProperty("class", "calendar-table");
      QPalette palette = table->palette();
      palette.setColor(QPalette::Highlight, QColor(0, 0, 0, 0));
      palette.setColor(QPalette::HighlightedText, palette.color(QPalette::Text));
      table->setPalette(palette);
    }
    if (!options_.locale.empty()) calendar->setLocale(QLocale(localeName()));
    calendar->setSelectedDate(today);
    calendar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    const auto selected = [this](const QDate& date) {
      if (!dayLabel_) return;
      const QLocale locale = qlocale();
      dayLabel_->setText(locale.dayName(date.dayOfWeek()));
      monthLabel_->setText(locale.monthName(date.month()));
      if (yearLabel_) yearLabel_->setText(QString::number(date.year()));
      dateLabel_->setText(QString::number(date.day()));
      if (weekLabel_) weekLabel_->setText(QStringLiteral("Week %1").arg(date.weekNumber()));
    };
    connect(calendar, &QCalendarWidget::currentPageChanged, this,
            [this, selected](int year, int month) {
              if (!calendar_ || !dayLabel_) return;
              const QLocale locale = qlocale();
              monthLabel_->setText(locale.monthName(month));
              if (yearLabel_) yearLabel_->setText(QString::number(year));
              int day = calendar_->selectedDate().day();
              day = std::min(day, QDate(year, month, 1).daysInMonth());
              const QDate date(year, month, day);
              dayLabel_->setText(locale.dayName(date.dayOfWeek()));
              dateLabel_->setText(QString::number(date.day()));
            });
    connect(calendar, &QCalendarWidget::clicked, this, selected);
    layout->addWidget(calendar);

    if (config.extended) {
      auto* actions = new QVBoxLayout;
      actions->setContentsMargins(0, 0, 0, 0);
      actions->setSpacing(0);
      actions->setAlignment(Qt::AlignTop);
      auto* right = new QFrame;
      right->setProperty("class", "extended-container");
      right->setLayout(actions);

      auto* alarmButton = new QPushButton(QStringLiteral("Set Alarm"));
      alarmButton->setProperty("class", "button alarm small");
      auto* timerButton = new QPushButton(QStringLiteral("Set Timer"));
      timerButton->setProperty("class", "button timer small");
      connect(alarmButton, &QPushButton::clicked, this, [this, popup = QPointer(popup)] {
        if (popup) popup->close();
        showAlarmDialog();
      });
      connect(timerButton, &QPushButton::clicked, this, [this, popup = QPointer(popup)] {
        if (popup) popup->close();
        showTimerDialog();
      });
      actions->addWidget(alarmButton);
      actions->addWidget(timerButton);

      auto* header = new QLabel(QStringLiteral("Upcoming holidays"));
      header->setProperty("class", "upcoming-events-header");
      header->setAlignment(Qt::AlignLeft);
      actions->addWidget(header);
      for (int i = 0; i < 4; ++i) {
        auto* item = new QLabel(QString());
        item->setProperty("class", "upcoming-events-item");
        item->setWordWrap(false);
        item->setAlignment(Qt::AlignLeft);
        actions->addWidget(item);
      }
      layout->addWidget(right);
    }

    positionPopup(popup);
    popup->show();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Clock calendar failed:" << error.what();
  }
}

void ClockWidget::showContextMenu() {
  try {
    auto* menu = new QMenu(window());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    // apply_qmenu_style: Fusion removes the menu shadow, blur and round corners on show.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
      fusion->setParent(menu);
      menu->setStyle(fusion);
    }
    connect(menu, &QMenu::aboutToShow, menu, [menu] {
      try {
        enableBlur(menu->winId(), false, true, QStringLiteral("normal"), QStringLiteral("None"));
      } catch (...) {
      }
    });
    menu->setProperty("class", "context-menu");

    if (timezoneList_.size() > 1) {
      auto* tzMenu = new QMenu(QStringLiteral("Timezones"), menu);
      tzMenu->setProperty("class", "context-menu submenu");
      for (const auto& tz : timezoneList_) {
        QAction* action = tzMenu->addAction(zoneDisplay(tz));
        connect(action, &QAction::triggered, this, [this, tz] { setTimezone(tz); });
      }
      menu->addMenu(tzMenu);
      menu->addSeparator();
    }

    if (state_.timerActive()) {
      connect(menu->addAction(QStringLiteral("Cancel Timer")), &QAction::triggered, this,
              [this] { state_.cancelTimer(); });
    } else {
      connect(menu->addAction(QStringLiteral("Set Timer")), &QAction::triggered, this,
              [this] { showTimerDialog(); });
    }
    menu->addSeparator();
    connect(menu->addAction(QStringLiteral("Set Alarm")), &QAction::triggered, this,
            [this] { showAlarmDialog(); });

    if (!state_.alarms().empty()) {
      menu->addSeparator();
      menu->addAction(QStringLiteral("Alarms"))->setEnabled(false);
      for (const Alarm& alarm : state_.alarms()) {
        QAction* action = menu->addAction(
            formatAlarmText(alarm, QString::fromStdString(options_.alarm_icons.enabled),
                            QString::fromStdString(options_.alarm_icons.disabled)));
        connect(action, &QAction::triggered, this, [this, alarm] { showAlarmDialog(&alarm); });
      }
    }

    const int margin = 6;
    const QSize size = menu->sizeHint();
    QWidget* bar = window();
    const QPoint barTopLeft = bar ? bar->mapToGlobal(bar->rect().topLeft()) : QPoint();
    const int barHeight = bar ? bar->height() : 0;
    const QPoint center = mapToGlobal(rect().center());
    const int x = center.x() - size.width() / 2;
    const bool barOnTop = property("bar_position").toString() == QLatin1String("top");
    const int y =
        barOnTop ? barTopLeft.y() + barHeight + margin : barTopLeft.y() - size.height() - margin;
    menu->popup(QPoint(x, y));
    menu->activateWindow();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Clock context menu failed:" << error.what();
  }
}

void ClockWidget::playSound(bool loop) {
  const QString path =
      QCoreApplication::applicationDirPath() + QStringLiteral("/assets/sound/notification02.wav");
  platform::playSound(QDir::toNativeSeparators(path).toStdWString(), loop);
  if (loop) QTimer::singleShot(kAlarmSoundLoopMs, this, [this] { stopAlarmSound(); });
}

void ClockWidget::stopAlarmSound() { platform::stopSound(); }

void ClockWidget::alarmFired(const Alarm& alarm) {
  playSound(true);
  try {
    showActiveAlarm(alarm);
  } catch (const std::exception& error) {
    qWarning().noquote() << "Failed to show active alarm popup:" << error.what();
  }
}

void ClockWidget::timerFinished() { playSound(false); }

}  // namespace yasb::widgets
