#include <windows.h>

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <memory>
#include <vector>

#include "core/bar/app_bar.h"
#include "core/widgets/backdrop.h"
#include "spike/host.h"

namespace {
constexpr int kBarHeight = 40;
}

int main(int argc, char* argv[]) {
  // As app/main.cpp setFontEngine(): Qt's DirectWrite font database maps every installed font
  // (about 1.7k handles and 100 MiB working set). Respect a platform the caller chose.
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "windows:fontengine=gdi");
  }
  // A transparent, blurred window needs an alpha swap chain.
  QQuickWindow::setDefaultAlphaBuffer(true);
  QGuiApplication app(argc, argv);

  QCommandLineParser parser;
  QCommandLineOption widgetsOpt(QStringLiteral("widgets"), QStringLiteral("User widget folder."),
                                QStringLiteral("dir"), QStringLiteral(SPIKE_WIDGETS_DIR));
  QCommandLineOption appBarOpt(QStringLiteral("register-app-bar"),
                               QStringLiteral("Reserve screen work area."));
  parser.addOption(widgetsOpt);
  parser.addOption(appBarOpt);
  parser.process(app);

  QQmlEngine engine;
  spike::Host host(&engine, parser.value(widgetsOpt));
  engine.rootContext()->setContextProperty(QStringLiteral("Host"), &host);

  QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/YasbSpike/qml/Bar.qml")));
  std::vector<std::unique_ptr<QObject>> bars;
  std::vector<std::unique_ptr<yasb::bar::IAppBar>> appBars;

  for (QScreen* screen : QGuiApplication::screens()) {
    std::unique_ptr<QObject> obj(component.createWithInitialProperties(
        {{QStringLiteral("screen"), QVariant::fromValue(screen)},
         {QStringLiteral("barHeight"), kBarHeight}}));
    auto* window = qobject_cast<QQuickWindow*>(obj.get());
    if (!window) {
      qCritical().noquote() << component.errorString();
      return 1;
    }
    window->show();
    const auto hwnd = static_cast<quintptr>(window->winId());
    yasb::widgets::enableBlur(hwnd, true, true);
    // Widgets match their window manager's monitor by this handle (Window.window.monitorHandle).
    // MONITOR_DEFAULTTONEAREST: the window was only just shown, so it may not be on a monitor yet.
    window->setProperty("monitorHandle",
                        static_cast<qlonglong>(reinterpret_cast<quintptr>(MonitorFromWindow(
                            reinterpret_cast<HWND>(hwnd), MONITOR_DEFAULTTONEAREST))));

    auto appBar = yasb::bar::makeWin32AppBar();
    yasb::bar::AppBarRequest req;
    req.hwnd = hwnd;
    req.height = kBarHeight;
    req.screen = screen;
    req.reserveSpace = parser.isSet(appBarOpt);
    req.alwaysOnTop = true;
    appBar->create(req);
    appBars.push_back(std::move(appBar));
    bars.push_back(std::move(obj));
  }

  const int rc = app.exec();
  for (auto& appBar : appBars) appBar->remove();
  return rc;
}
