#include "core/bar/bar_window.h"

#include <QCloseEvent>
#include <QEasingCurve>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTimer>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace yasb::bar {

namespace {

using nlohmann::json;

template <typename T>
void read(const json& j, const char* key, T& out) {
  const auto it = j.find(key);
  if (it == j.end() || it->is_null()) return;
  try {
    out = it->get<T>();
  } catch (const json::exception&) {
  }
}

LayoutSettings readLayout(const json& j, const char* key, LayoutSettings def) {
  const auto it = j.find(key);
  if (it != j.end() && it->is_object()) {
    read(*it, "alignment", def.alignment);
    read(*it, "stretch", def.stretch);
  }
  return def;
}

bool isValidPercentage(const std::string& s) {
  if (s.size() < 2 || s.size() > 4 || s.back() != '%') return false;
  return std::all_of(s.begin(), s.end() - 1, [](unsigned char c) { return std::isdigit(c); });
}

}  // namespace

BarSettings BarSettings::fromJson(const json& bar) {
  BarSettings s;
  read(bar, "class_name", s.className);
  read(bar, "style", s.style);
  read(bar, "style_adaptive_exclude", s.styleAdaptiveExclude);
  if (const auto it = bar.find("alignment"); it != bar.end() && it->is_object()) {
    read(*it, "position", s.position);
    read(*it, "align", s.align);
  }
  if (const auto it = bar.find("dimensions"); it != bar.end() && it->is_object()) {
    read(*it, "height", s.height);
    if (const auto w = it->find("width"); w != it->end()) {
      if (w->is_string()) {
        s.width = w->get<std::string>();
      } else if (w->is_number()) {
        s.width = std::to_string(w->get<int>());
      }
    }
  }
  if (const auto it = bar.find("padding"); it != bar.end() && it->is_object()) {
    read(*it, "left", s.paddingLeft);
    read(*it, "right", s.paddingRight);
    read(*it, "top", s.paddingTop);
    read(*it, "bottom", s.paddingBottom);
  }
  if (const auto it = bar.find("animation"); it != bar.end() && it->is_object()) {
    read(*it, "enabled", s.animationEnabled);
    read(*it, "duration", s.animationDuration);
  }
  if (const auto it = bar.find("window_flags"); it != bar.end() && it->is_object()) {
    read(*it, "always_on_top", s.alwaysOnTop);
    read(*it, "windows_app_bar", s.windowsAppBar);
  }
  if (const auto it = bar.find("layouts"); it != bar.end() && it->is_object()) {
    s.left = readLayout(*it, "left", s.left);
    s.center = readLayout(*it, "center", s.center);
    s.right = readLayout(*it, "right", s.right);
  }
  return s;
}

BarWindow::BarWindow(QString barId, QString barName, QScreen* screen, const QString& stylesheet,
                     const Widgets& widgets, BarSettings settings, BarDeps deps)
    : QWidget(nullptr),
      barId_(std::move(barId)),
      barName_(std::move(barName)),
      screenName_(screen->name()),
      screen_(screen),
      settings_(std::move(settings)),
      deps_(std::move(deps)),
      devicePixelRatio_(screen->devicePixelRatio()),
      autoWidth_(QString::fromStdString(settings_.width).toLower() == QLatin1String("auto")) {
  hide();
  setScreen(screen);
  setWindowTitle(QStringLiteral("YasbBar"));
  setStyleSheet(stylesheet);
  setWindowFlag(Qt::Tool);
  setWindowFlag(Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_DeleteOnClose);

  const QString className = QString::fromStdString(settings_.className);
  if (settings_.style == "adaptive") {
    adaptive_ = new AdaptiveBarFrame(this, settings_.position, settings_.styleAdaptiveExclude);
    frame_ = adaptive_;
    frame_->setProperty("class", QStringLiteral("bar %1 adaptive").arg(className));
    adaptive_->setRepositionHandler([this] { positionBar(); });
  } else {
    frame_ = new BarFrame(this);
    frame_->setProperty("class", QStringLiteral("bar %1").arg(className));
  }
  setProperty("cssEngineDisableShadow", true);
  frame_->setProperty("cssEngineDisableShadow", true);
  frame_->setCursor(Qt::ArrowCursor);

  if (settings_.alwaysOnTop) setWindowFlag(Qt::WindowStaysOnTopHint);

  theme_ = new OsThemeManager(frame_, this, deps_.themeDetector, deps_.globalState);
  theme_->updateThemeClass();

  positionBar();
  addWidgets(widgets);

  if (autoWidth_) {
    frame_->installEventFilter(this);
    QTimer::singleShot(0, this, [this] { applyAutoWidth(autoWidth()); });
  }

  connect(
      screen_, &QScreen::geometryChanged, this, [this] { onGeometryChanged(); },
      Qt::QueuedConnection);
  updateAppBar();
}

BarWindow::~BarWindow() = default;

QRect BarWindow::screenRect() const {
  return deps_.screenGeometry ? deps_.screenGeometry() : screen_->geometry();
}

void BarWindow::onGeometryChanged() {
  if (screen_->devicePixelRatio() != devicePixelRatio_) {
    emit restartRequested(QStringLiteral("Reloading Application because of screen DPI change."));
    return;
  }
  positionBar();
  updateAppBar();
  if (autoWidth_) QTimer::singleShot(0, this, [this] { applyAutoWidth(autoWidth()); });
}

void BarWindow::updateAppBar() {
  if (!deps_.appBar) return;
  AppBarRequest r;
  r.hwnd = static_cast<std::uintptr_t>(winId());
  r.edge = settings_.position == "top" ? AppBarEdge::Top : AppBarEdge::Bottom;
  r.height = settings_.height + settings_.paddingTop + settings_.paddingBottom;
  r.screen = screen_;
  r.scaleScreenHeight = screen_->devicePixelRatio() > 1.0;
  r.barName = barName_;
  r.reserveSpace = settings_.windowsAppBar;
  r.alwaysOnTop = settings_.alwaysOnTop;
  deps_.appBar->create(r);
}

void BarWindow::removeAppBar() {
  if (deps_.appBar) deps_.appBar->remove();
}

QPoint BarWindow::barPos(int barW, int barH, int screenW, int screenH) const {
  const QRect g = screenRect();
  const int sx = g.x();
  const int sy = g.y();
  int x;
  if (settings_.align == "center") {
    const int availableX = sx + settings_.paddingLeft;
    const int availableW = screenW - settings_.paddingLeft - settings_.paddingRight;
    x = barW >= availableW ? availableX : static_cast<int>(availableX + (availableW - barW) / 2.0);
  } else if (settings_.align == "right") {
    x = sx + screenW - barW - settings_.paddingRight;
    x = std::max(x, sx + settings_.paddingLeft);
  } else {
    x = sx + settings_.paddingLeft;
    x = std::min(x, sx + screenW - barW - settings_.paddingRight);
  }
  const int y = settings_.position == "bottom" ? sy + screenH - barH - settings_.paddingBottom
                                               : sy + settings_.paddingTop;
  return {x, y};
}

int BarWindow::autoWidth() {
  if (QLayout* l = frame_->layout()) l->activate();
  const int requested = std::max(frame_->sizeHint().width(), 0);
  const int available = screenRect().width() - settings_.paddingLeft - settings_.paddingRight;
  currentAutoWidth_ = std::min(requested, available);
  return currentAutoWidth_;
}

void BarWindow::applyAutoWidth(int width) {
  if (width < 0) return;
  const QRect g = screenRect();
  const QPoint p = barPos(width, settings_.height, g.width(), g.height());
  frame_->setGeometry(0, 0, width, settings_.height);
  setGeometry(p.x(), p.y(), width, settings_.height);
}

int BarWindow::resolveWidth(int screenW) const {
  if (isValidPercentage(settings_.width)) {
    const double percent = std::stod(settings_.width.substr(0, settings_.width.size() - 1)) / 100.0;
    return static_cast<int>(screenW * percent);
  }
  try {
    return std::stoi(settings_.width);
  } catch (const std::exception&) {
    return screenW;
  }
}

void BarWindow::positionBar() {
  const QRect g = screenRect();
  const int screenW = g.width();
  const int screenH = g.height();
  const int barHeight = settings_.height;

  int barWidth;
  if (autoWidth_) {
    barWidth = frame_->layout() ? autoWidth() : 0;
  } else {
    barWidth = resolveWidth(screenW);
  }

  const int available = screenW - settings_.paddingLeft - settings_.paddingRight;
  barWidth = std::min(barWidth, available);

  QPoint pos = barPos(barWidth, barHeight, screenW, screenH);

  // Only the window grows for the edge curves; the app bar is still told dimensions.height
  int extra = 0;
  if (adaptive_) {
    const bool spansScreen = barWidth >= screenW && !autoWidth_;
    extra = adaptive_->useEdgeCurves(spansScreen);
  }
  if (extra && settings_.position == "bottom") pos.ry() -= extra;

  frame_->setGeometry(0, 0, barWidth, barHeight + extra);
  setGeometry(pos.x(), pos.y(), barWidth, barHeight + extra);
}

void BarWindow::addWidgets(const Widgets& widgets) {
  auto* grid = new QGridLayout;
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(0);

  const struct {
    const char* name;
    const LayoutSettings* cfg;
  } columns[] = {
      {"left", &settings_.left}, {"center", &settings_.center}, {"right", &settings_.right}};

  int column = 0;
  for (const auto& col : columns) {
    auto* layout = new QHBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* container = new QFrame;
    container->setProperty("class",
                           QStringLiteral("container container-%1").arg(QLatin1String(col.name)));

    for (QWidget* w : widgets.value(QLatin1String(col.name))) {
      w->setProperty("yasb_layout_type", QLatin1String(col.name));
      w->setProperty("bar_id", barId_);
      layout->addWidget(w, 0);
    }

    const auto& cfg = *col.cfg;
    if (cfg.alignment == "left" && cfg.stretch) {
      layout->addStretch(1);
    } else if (cfg.alignment == "right" && cfg.stretch) {
      layout->insertStretch(0, 1);
    } else if (cfg.alignment == "center" && cfg.stretch) {
      layout->insertStretch(0, 1);
      layout->addStretch(1);
    }

    container->setLayout(layout);
    grid->addWidget(container, 0, column++);
  }

  if (adaptive_) {
    adaptive_->setBarLayout(grid);
  } else {
    frame_->setLayout(grid);
  }
}

void BarWindow::showEvent(QShowEvent* e) {
  QWidget::showEvent(e);
  if (settings_.animationEnabled && initialShow_) {
    initialShow_ = false;
    // Fade rather than slide on first show: DWM flashes blur and shadow with a slide
    setWindowOpacity(0.0);
    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(settings_.animationDuration);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutQuad);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
  }
}

void BarWindow::closeEvent(QCloseEvent* e) {
  removeAppBar();
  QWidget::closeEvent(e);
}

void BarWindow::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  // On a DPI change Qt keeps the old native size when the logical size is unchanged
  if (e->size() != frame_->size()) QTimer::singleShot(0, this, [this] { positionBar(); });
}

void BarWindow::changeEvent(QEvent* e) {
  if (e->type() == QEvent::PaletteChange && theme_) theme_->updateThemeClass();
  QWidget::changeEvent(e);
}

bool BarWindow::eventFilter(QObject* watched, QEvent* e) {
  if (autoWidth_ && watched == frame_ && e->type() == QEvent::LayoutRequest) {
    const int previous = currentAutoWidth_;
    const int next = autoWidth();
    if (next != previous) applyAutoWidth(next);
  }
  return QWidget::eventFilter(watched, e);
}

}  // namespace yasb::bar
