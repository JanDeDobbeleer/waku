// Alarm editor, timer dialog and the active-alarm window of the clock widget
// (ClockWidget._show_alarm_dialog, _show_timer_dialog, _show_active_alarm_popup).
#include <QApplication>
#include <QButtonGroup>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

#include "core/bar/theme_class.h"
#include "core/widgets/backdrop.h"
#include "widgets/clock/clock_widget.h"

namespace yasb::widgets {
namespace {

// Two-digit value, read-only text, selection colours made invisible once shown.
class FormattedSpinBox : public QSpinBox {
 public:
  explicit FormattedSpinBox(QWidget* parent = nullptr) : QSpinBox(parent) {
    if (QLineEdit* edit = lineEdit()) edit->setReadOnly(true);
  }

 protected:
  QString textFromValue(int value) const override {
    return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0'));
  }
  void showEvent(QShowEvent* event) override {
    QSpinBox::showEvent(event);
    if (QLineEdit* edit = lineEdit()) {
      const QColor color = palette().color(foregroundRole());
      edit->setStyleSheet(QStringLiteral("QLineEdit { selection-background-color: transparent; "
                                         "selection-color: %1; }")
                              .arg(color.name()));
    }
  }
};

const char* const kDayNames[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

void setButtonClass(QPushButton* button, const QString& cssClass) {
  button->setProperty("class", cssClass);
  yasb::bar::refreshWidgetStyle(button);
}

}  // namespace

QHBoxLayout* ClockWidget::timeGrid(const QString& label1, const QString& label2, int max1, int max2,
                                   int value1, int value2, QSpinBox** spin1, QSpinBox** spin2) {
  auto* grid = new QGridLayout;
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(0);
  grid->setVerticalSpacing(0);

  const auto caption = [&](const QString& text, Qt::Alignment alignment, int column) {
    auto* label = new QLabel(text);
    label->setAlignment(alignment);
    label->setProperty("class", "clock-label-timer");
    grid->addWidget(label, 0, column);
  };
  caption(label1, Qt::AlignRight, 0);
  caption(label2, Qt::AlignLeft, 2);

  const auto makeSpin = [&](int max, Qt::Alignment alignment, int value, int column) {
    auto* spin = new FormattedSpinBox;
    spin->setRange(0, max);
    spin->setAlignment(alignment);
    spin->setProperty("class", "clock-input-time");
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setWrapping(true);
    spin->setValue(value);
    grid->addWidget(spin, 1, column);
    return spin;
  };
  *spin1 = makeSpin(max1, Qt::AlignRight, value1, 0);

  auto* colon = new QLabel(QStringLiteral(":"));
  colon->setAlignment(Qt::AlignCenter);
  colon->setProperty("class", "clock-input-time colon");
  grid->addWidget(colon, 1, 1);

  *spin2 = makeSpin(max2, Qt::AlignLeft, value2, 2);

  auto* wrapper = new QHBoxLayout;
  wrapper->addStretch();
  wrapper->addLayout(grid);
  wrapper->addStretch();
  return wrapper;
}

QFrame* ClockWidget::footer(const std::vector<QPushButton*>& buttons) {
  auto* frame = new QFrame;
  frame->setProperty("class", "clock-popup-footer");
  auto* layout = new QHBoxLayout(frame);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  for (QPushButton* button : buttons) {
    layout->addWidget(button);
    if (button->text() == QLatin1String("Delete")) layout->addStretch(1);
  }
  return frame;
}

void ClockWidget::clearQuickGroup(QButtonGroup* group) {
  if (!group) return;
  const bool exclusive = group->exclusive();
  group->setExclusive(false);
  for (QAbstractButton* button : group->buttons()) button->setChecked(false);
  group->setExclusive(exclusive);
}

void ClockWidget::showAlarmDialog(const Alarm* existing) {
  try {
    if (alarmPopup_) alarmPopup_->deleteLater();
    PinnablePopup* popup = newPopup(QStringLiteral("clock-popup alarm"));
    alarmPopup_ = popup;
    const bool editing = existing != nullptr;
    const Alarm original = editing ? *existing : Alarm::object();

    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* container = new QFrame;
    container->setProperty("class", "clock-popup-container");
    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    int hour = 0;
    int minute = 0;
    if (editing) {
      const QString time = QString::fromStdString(alarmTime(original));
      hour = time.section(QLatin1Char(':'), 0, 0).toInt();
      minute = time.section(QLatin1Char(':'), 1, 1).toInt();
    } else {
      const QTime current = QTime::currentTime();
      hour = current.hour();
      minute = current.minute();
    }
    QSpinBox* hourSpin = nullptr;
    QSpinBox* minuteSpin = nullptr;
    containerLayout->addLayout(timeGrid(QStringLiteral("Hour"), QStringLiteral("Minute"), 23, 59,
                                        hour, minute, &hourSpin, &minuteSpin));

    auto* daysLayout = new QHBoxLayout;
    std::vector<QPushButton*> dayButtons;
    const std::vector<int> savedDays = editing ? alarmDays(original) : std::vector<int>();
    for (int i = 0; i < 7; ++i) {
      auto* button = new QPushButton(QString::fromLatin1(kDayNames[i]));
      button->setCheckable(true);
      button->setProperty("class", "button day");
      if (editing) {
        button->setChecked(std::find(savedDays.begin(), savedDays.end(), i) != savedDays.end());
      }
      dayButtons.push_back(button);
      daysLayout->addWidget(button);
    }
    containerLayout->addLayout(daysLayout);
    const int todayIndex = QDate::currentDate().dayOfWeek() - 1;
    if (!editing) {
      for (int i = 0; i < 7; ++i) dayButtons[static_cast<size_t>(i)]->setChecked(i == todayIndex);
    }

    auto* quickLayout = new QHBoxLayout;
    QPushButton* enabledButton = nullptr;
    if (editing) {
      enabledButton = new QPushButton;
      enabledButton->setCheckable(true);
      const bool enabled = alarmEnabled(original);
      enabledButton->setChecked(enabled);
      enabledButton->setText(enabled ? QStringLiteral("Enabled") : QStringLiteral("Disabled"));
      enabledButton->setProperty("class", QStringLiteral("button %1")
                                              .arg(enabled ? QStringLiteral("is-alarm-enabled")
                                                           : QStringLiteral("is-alarm-disabled")));
      connect(enabledButton, &QPushButton::toggled, enabledButton, [enabledButton](bool checked) {
        enabledButton->setText(checked ? QStringLiteral("Enabled") : QStringLiteral("Disabled"));
        setButtonClass(enabledButton, QStringLiteral("button %1")
                                          .arg(checked ? QStringLiteral("is-alarm-enabled")
                                                       : QStringLiteral("is-alarm-disabled")));
      });
      quickLayout->addWidget(enabledButton);
    }

    auto* titleEdit = new QLineEdit;
    auto* saveButton = new QPushButton(QStringLiteral("Save"));
    const auto updateSave = [dayButtons, titleEdit, saveButton] {
      const bool anyDay = std::any_of(dayButtons.begin(), dayButtons.end(),
                                      [](QPushButton* b) { return b->isChecked(); });
      saveButton->setEnabled(!titleEdit->text().trimmed().isEmpty() && anyDay);
    };

    auto* quickGroup = new QButtonGroup(popup);
    quickGroup->setExclusive(true);
    const struct {
      const char* label;
      int kind;
    } quickOptions[] = {{"Today", 0}, {"Every day", 1}, {"Weekdays", 2}};
    for (const auto& option : quickOptions) {
      auto* button = new QPushButton(QString::fromLatin1(option.label));
      button->setCheckable(true);
      button->setProperty("class", "button quick-option");
      quickGroup->addButton(button);
      const int kind = option.kind;
      connect(button, &QPushButton::clicked, this, [=] {
        for (QPushButton* day : dayButtons) day->blockSignals(true);
        for (int i = 0; i < 7; ++i) {
          const bool on = kind == 0   ? i == QDate::currentDate().dayOfWeek() - 1
                          : kind == 1 ? true
                                      : i < 5;
          dayButtons[static_cast<size_t>(i)]->setChecked(on);
        }
        for (QPushButton* day : dayButtons) day->blockSignals(false);
        updateSave();
      });
      quickLayout->addWidget(button);
    }
    for (QPushButton* day : dayButtons) {
      connect(day, &QPushButton::clicked, this, [quickGroup] { clearQuickGroup(quickGroup); });
      connect(day, &QPushButton::clicked, this, updateSave);
    }
    containerLayout->addLayout(quickLayout);

    auto* titleLayout = new QHBoxLayout;
    titleEdit->setProperty("class", "alarm-input-title");
    titleEdit->setPlaceholderText(QStringLiteral("Alarm title"));
    titleEdit->setContextMenuPolicy(Qt::NoContextMenu);
    if (editing) titleEdit->setText(QString::fromStdString(alarmTitle(original)));
    titleLayout->addWidget(titleEdit);
    containerLayout->addLayout(titleLayout);

    std::vector<QPushButton*> buttons;
    if (editing) {
      auto* deleteButton = new QPushButton(QStringLiteral("Delete"));
      deleteButton->setProperty("class", "button delete");
      connect(deleteButton, &QPushButton::clicked, this, [this, popup, original] {
        state_.removeAlarm(original);
        popup->close();
      });
      buttons.push_back(deleteButton);
    }
    saveButton->setProperty("class", "button save");
    auto* cancelButton = new QPushButton(QStringLiteral("Cancel"));
    cancelButton->setProperty("class", "button cancel");
    buttons.push_back(saveButton);
    buttons.push_back(cancelButton);

    connect(saveButton, &QPushButton::clicked, this,
            [this, popup, original, editing, hourSpin, minuteSpin, dayButtons, enabledButton,
             titleEdit] {
              nlohmann::ordered_json days = nlohmann::ordered_json::array();
              for (int i = 0; i < 7; ++i) {
                if (dayButtons[static_cast<size_t>(i)]->isChecked()) days.push_back(i);
              }
              if (days.empty()) days.push_back(QDate::currentDate().dayOfWeek() - 1);
              Alarm data = Alarm::object();
              data["time"] = QStringLiteral("%1:%2")
                                 .arg(hourSpin->value(), 2, 10, QLatin1Char('0'))
                                 .arg(minuteSpin->value(), 2, 10, QLatin1Char('0'))
                                 .toStdString();
              data["days"] = days;
              data["enabled"] = enabledButton ? enabledButton->isChecked() : true;
              data["title"] = titleEdit->text().trimmed().toStdString();
              if (editing) {
                state_.updateAlarm(original, std::move(data));
              } else {
                state_.addAlarm(std::move(data));
              }
              popup->close();
            });
    connect(titleEdit, &QLineEdit::textChanged, this, updateSave);
    connect(cancelButton, &QPushButton::clicked, popup, &QWidget::close);
    updateSave();

    layout->addWidget(container);
    layout->addWidget(footer(buttons));
    positionPopup(popup);
    popup->show();
    popup->activateWindow();
    titleEdit->setFocus();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Clock alarm dialog failed:" << error.what();
  }
}

void ClockWidget::showTimerDialog() {
  try {
    if (timerPopup_) timerPopup_->deleteLater();
    PinnablePopup* popup = newPopup(QStringLiteral("clock-popup timer"));
    timerPopup_ = popup;

    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* container = new QFrame;
    container->setProperty("class", "clock-popup-container");
    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    QSpinBox* minutes = nullptr;
    QSpinBox* seconds = nullptr;
    containerLayout->addLayout(timeGrid(QStringLiteral("Minutes"), QStringLiteral("Seconds"), 99,
                                        59, 5, 0, &minutes, &seconds));

    auto* quickLayout = new QHBoxLayout;
    auto* quickGroup = new QButtonGroup(popup);
    quickGroup->setExclusive(true);
    const struct {
      const char* label;
      int minutes;
    } quickOptions[] = {{"1 min", 1}, {"5 min", 5}, {"10 min", 10}, {"30 min", 30}, {"60 min", 60}};
    for (const auto& option : quickOptions) {
      auto* button = new QPushButton(QString::fromLatin1(option.label));
      button->setCheckable(true);
      button->setProperty("class", "button quick-option");
      quickGroup->addButton(button);
      const int value = option.minutes;
      connect(button, &QPushButton::clicked, this, [minutes, seconds, value] {
        minutes->blockSignals(true);
        seconds->blockSignals(true);
        minutes->setValue(value);
        seconds->setValue(0);
        minutes->blockSignals(false);
        seconds->blockSignals(false);
      });
      quickLayout->addWidget(button);
    }
    const auto clearQuick = [quickGroup] { clearQuickGroup(quickGroup); };
    connect(minutes, &QSpinBox::valueChanged, this, clearQuick);
    connect(seconds, &QSpinBox::valueChanged, this, clearQuick);
    containerLayout->addLayout(quickLayout);

    auto* startButton = new QPushButton(QStringLiteral("Start"));
    startButton->setProperty("class", "button start");
    auto* cancelButton = new QPushButton(QStringLiteral("Cancel"));
    cancelButton->setProperty("class", "button cancel");
    connect(startButton, &QPushButton::clicked, this, [this, popup, minutes, seconds] {
      const int total = minutes->value() * 60 + seconds->value();
      if (total <= 0) return;
      state_.startTimer(total);
      popup->close();
    });
    connect(cancelButton, &QPushButton::clicked, popup, &QWidget::close);

    layout->addWidget(container);
    layout->addWidget(footer({startButton, cancelButton}));
    positionPopup(popup);
    popup->show();
    popup->activateWindow();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Clock timer dialog failed:" << error.what();
  }
}

void ClockWidget::showActiveAlarm(const Alarm& alarm) {
  if (activeAlarm_) activeAlarm_->deleteLater();
  auto* window = new QFrame(this);
  activeAlarm_ = window;
  window->setAttribute(Qt::WA_DeleteOnClose);
  window->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
  enableBlur(window->winId(), true, true, QStringLiteral("normal"), QStringLiteral("None"));
  window->setProperty("class", "active-alarm-window");
  window->activateWindow();

  auto* layout = new QVBoxLayout(window);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->setAlignment(Qt::AlignTop);

  auto* icon = new QLabel(QString::fromStdString(options_.alarm_icons.disabled));
  icon->setProperty("class", "alarm-title-icon");
  icon->setAlignment(Qt::AlignCenter);
  const std::string titleText = alarmTitle(alarm);
  auto* title =
      new QLabel(titleText.empty() ? QStringLiteral("Alarm") : QString::fromStdString(titleText));
  title->setWordWrap(true);
  title->setProperty("class", "alarm-title");
  title->setAlignment(Qt::AlignCenter);
  title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  layout->addWidget(icon);
  layout->addWidget(title);

  const Moment moment = now();
  auto* info = new QLabel(moment.when.toString(QStringLiteral("HH:mm")));
  info->setProperty("class", "alarm-info");
  info->setAlignment(Qt::AlignCenter);
  layout->addWidget(info);

  auto* buttonLayout = new QHBoxLayout;
  const struct {
    const char* label;
    int minutes;
  } choices[] = {{"Stop", 0}, {"Snooze 1 min", 1}, {"Snooze 3 min", 3}, {"Snooze 5 min", 5}};
  for (const auto& choice : choices) {
    auto* button = new QPushButton(QString::fromLatin1(choice.label));
    button->setProperty("class", "button");
    buttonLayout->addWidget(button);
    const int minutes = choice.minutes;
    connect(button, &QPushButton::clicked, this, [this, window, alarm, minutes] {
      stopAlarmSound();
      if (minutes == 0) {
        state_.clearSnooze(alarm);
      } else {
        state_.snooze(alarm, minutes);
      }
      window->close();
    });
  }
  layout->addLayout(buttonLayout);
  window->adjustSize();

  const QScreen* screen = QApplication::screenAt(mapToGlobal(rect().center()));
  if (!screen) screen = QApplication::primaryScreen();
  if (screen) {
    const QRect geometry = screen->availableGeometry();
    window->move(geometry.x() + (geometry.width() - window->width()) / 2,
                 geometry.y() + (geometry.height() - window->height()) / 2);
  }
  window->show();
}

}  // namespace yasb::widgets
