#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>

#include "features/tiling/tiling_command.h"
#include "features/tiling/tiling_engine.h"
#include "features/tiling/tiling_events.h"
#include "features/tiling/window_placer.h"
#include "kernel/qt/sealed_singleton.h"

class QJSEngine;
class QQmlEngine;
class QScreen;

namespace waku::tiling {

// The built-in tiler as one QML singleton: on by default in the waku engine, off with --no-tiling
// or when the shell sets `enabled = false`. While on, a window that opens is placed in the slot of
// its layout, per monitor and per virtual desktop, with documented Win32 only (WinEvent hooks,
// SetWindowPos), no polling and no injected DLL. Built on TilingEngine (tiling_engine.h); this
// class owns its inputs and the settings. GUI thread only.
//
//   enabled   the switch. Off: nothing is hooked and no window is touched
//   layout    the layout of a workspace with none of its own: "mainStack" (default),
//             "mainStackTop", "columns", "rows", "bsp", "grid" or "centerMain" (see layouts())
//   gap       pixels between windows and around them (default 8)
//   ratio     the share of the width of the main window (default 0.5; a height share in
//   mainStackTop) rules     [{process, class, title, action}]: `action` is "ignore" (default) or
//   "float";
//             `process` and `class` match whole and without case, `title` is a regular expression.
//             No rule ships by default beyond the built-in splash and shell classes
//   active    true while the tiler runs: enabled and on the live desktop
//
// A shell turns it off with
//   Component.onCompleted: { Tiling.enabled = false }
// The `tile` verb of `wakuc` drives the same functions (tiling_command.h).
class Tiling : public QObject, public TilingControl {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  QML_EXTENDED(waku::kernel::SealedSingleton)
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
  Q_PROPERTY(QString layout READ layout WRITE setLayoutName NOTIFY layoutChanged FINAL)
  Q_PROPERTY(int gap READ gap WRITE setGap NOTIFY gapChanged FINAL)
  Q_PROPERTY(double ratio READ ratio WRITE setRatio NOTIFY ratioChanged FINAL)
  Q_PROPERTY(QVariantList rules READ rules WRITE setRules NOTIFY rulesChanged FINAL)
  Q_PROPERTY(bool active READ active NOTIFY activeChanged FINAL)
  Q_PROPERTY(QStringList layouts READ layouts CONSTANT FINAL)
  Q_PROPERTY(QString currentLayout READ currentLayout NOTIFY currentLayoutChanged FINAL)

 public:
  // Everything the service needs from the machine. A null placer or hooks (the offscreen
  // platform, `--no-tiling`) leaves the service inert.
  struct Deps {
    std::unique_ptr<WindowPlacer> placer;
    std::unique_ptr<TilingHooks> hooks;
    std::unique_ptr<Scheduler> scheduler;
    bool selfElevated = false;
    bool watchScreens = true;
    int screenDebounceMs = 400;
  };

  // The real service: the machine's inputs when kernel::liveDesktop("tiling"), none otherwise.
  explicit Tiling(QObject* parent = nullptr);
  Tiling(Deps deps, QObject* parent);
  ~Tiling() override;
  // The engine's own singleton.
  static Tiling* create(QQmlEngine* engine, QJSEngine* scriptEngine);

  bool enabled() const { return enabled_; }
  void setEnabled(bool enabled);
  QString layout() const { return settings_.layout; }
  void setLayoutName(const QString& name);
  int gap() const { return settings_.gap; }
  void setGap(int gap);
  double ratio() const { return settings_.ratio; }
  void setRatio(double ratio);
  QVariantList rules() const { return rules_; }
  void setRules(const QVariantList& rules);
  bool active() const { return active_; }
  QStringList layouts() const;
  // The layout of the workspace in front (the focused window's desktop and monitor); empty while
  // the tiler is not active. Follows the focus and every layout change.
  QString currentLayout() const { return currentLayout_; }

  // Floats a tiled focused window, or tiles a floating one.
  Q_INVOKABLE void toggleFloat();
  // Places every window of the shown desktop again.
  Q_INVOKABLE void retile();
  // Sets the layout of the focused window's workspace; false for an unknown name.
  Q_INVOKABLE bool setLayout(const QString& name);
  // Steps the layout of the focused workspace through layouts(): 1 next, -1 previous. Returns the
  // new layout, or empty when the tiler is not active.
  Q_INVOKABLE QString cycleLayout(int step = 1);
  Q_INVOKABLE void focusNext();
  Q_INVOKABLE void focusPrevious();

  // The engine, null when inert. For tests.
  TilingEngine* engine() { return engine_.get(); }

  // TilingControl
  QString controlFloat(std::optional<bool> floating) override;
  QString controlLayout(const QString& name) override;
  QString controlLayoutStep(int step) override;
  QString controlRetile() override;
  QString controlFocus(int direction) override;
  QString controlStatus() override;
  QString controlFocusDirection(Direction direction) override;
  QString controlMove(Direction direction) override;
  QString controlPromote() override;
  QString controlStack(Direction direction) override;
  QString controlUnstack() override;
  QString controlCycleStack(int step) override;
  QString controlResize(Axis axis, int sign) override;
  QString controlFlip(Axis axis) override;
  QString controlMonocle() override;
  QString controlPause() override;
  QString controlClose() override;
  QString controlMinimize() override;

 public slots:
  // Re-reads whether the tiler may run: after `enabled` changes and every few seconds while it is
  // on (a window manager that starts or stops).
  void reevaluate();
  // A screen was added, removed or changed its work area: retile after a short wait.
  void screensChanged();

 signals:
  void enabledChanged();
  void layoutChanged();
  void gapChanged();
  void ratioChanged();
  void rulesChanged();
  void activeChanged();
  void currentLayoutChanged();

 private:
  void init(Deps deps);
  void onEvent(const WindowEvent& event);
  void pushSettings();
  void refreshCurrentLayout();
  void watchScreen(QScreen* screen);
  QString notActive() const;
  // Runs an engine command when the tiler is active; the answer line.
  QString run(const std::function<CommandResult(TilingEngine&)>& command);

  std::unique_ptr<WindowPlacer> placer_;
  std::unique_ptr<TilingHooks> hooks_;
  std::unique_ptr<Scheduler> scheduler_;
  std::unique_ptr<TilingEngine> engine_;
  QString currentLayout_;
  QTimer screenTimer_;
  TilingSettings settings_;
  QVariantList rules_;
  bool enabled_ = false;
  bool active_ = false;
  bool watchScreens_ = true;
};

}  // namespace waku::tiling
