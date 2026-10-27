#pragma once

#include <QList>
#include <QMap>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

#include "core/bar/adaptive_frame.h"
#include "core/bar/app_bar.h"
#include "core/bar/theme_class.h"

class QGridLayout;

namespace yasb::bar {

struct LayoutSettings {
  std::string alignment = "left";
  bool stretch = true;
};

// Bar settings the window needs, read from one normalized config["bars"][name] object.
struct BarSettings {
  std::string className;
  std::string position = "top";
  std::string align = "center";
  int height = 30;
  std::string width = "100%";  // "NN%", "auto" or a pixel count
  int paddingLeft = 0, paddingRight = 0, paddingTop = 0, paddingBottom = 0;
  std::string style = "bar";  // "bar" or "adaptive"
  std::vector<std::string> styleAdaptiveExclude;
  bool animationEnabled = false;
  int animationDuration = 300;
  bool alwaysOnTop = true;
  bool windowsAppBar = false;
  LayoutSettings left{"left", true}, center{"center", true}, right{"right", true};

  static BarSettings fromJson(const nlohmann::json& bar);
};

// Test and tooling seams: nothing here touches the live desktop unless the caller says so.
struct BarDeps {
  std::unique_ptr<IAppBar> appBar;  // null: no app-bar registration at all
  OsThemeManager::Detector themeDetector = detectOsDarkTheme;
  std::function<QRect()> screenGeometry;  // default: target screen geometry
  GlobalState* globalState = nullptr;
};

// The bar window (port of core/bar.py Bar): a translucent frameless tool window holding the
// bar frame and the left/center/right containers.
class BarWindow : public QWidget {
  Q_OBJECT

 public:
  using Widgets = QMap<QString, QList<QWidget*>>;  // "left" | "center" | "right"

  BarWindow(QString barId, QString barName, QScreen* screen, const QString& stylesheet,
            const Widgets& widgets, BarSettings settings, BarDeps deps = {});
  ~BarWindow() override;

  const QString& barId() const { return barId_; }
  const QString& barName() const { return barName_; }
  const QString& screenName() const { return screenName_; }
  BarFrame* barFrame() const { return frame_; }
  AdaptiveBarFrame* adaptiveFrame() const { return adaptive_; }
  OsThemeManager* themeManager() const { return theme_; }
  const BarSettings& settings() const { return settings_; }
  QRect screenRect() const;

  // Window origin for a bar of the given size (Bar.bar_pos).
  QPoint barPos(int barW, int barH, int screenW, int screenH) const;
  void positionBar();
  void updateAppBar();
  void removeAppBar();

 signals:
  // A DPI change on a connected screen fires no screenAdded/Removed, so the owner must rebuild.
  void restartRequested(const QString& reason);

 protected:
  void showEvent(QShowEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  int autoWidth();
  void applyAutoWidth(int width);
  void addWidgets(const Widgets& widgets);
  void onGeometryChanged();
  int resolveWidth(int screenW) const;

  QString barId_;
  QString barName_;
  QString screenName_;
  QScreen* screen_;
  BarSettings settings_;
  BarDeps deps_;
  double devicePixelRatio_;
  bool autoWidth_;
  bool initialShow_ = true;
  int currentAutoWidth_ = 0;
  BarFrame* frame_ = nullptr;
  AdaptiveBarFrame* adaptive_ = nullptr;
  OsThemeManager* theme_ = nullptr;
};

}  // namespace yasb::bar
