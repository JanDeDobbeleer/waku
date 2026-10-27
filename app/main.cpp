#include <windows.h>

#include <winrt/base.h>

#include <QApplication>
#include <QCoreApplication>
#include <QPixmap>
#include <QPointer>
#include <QString>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "app/app_services.h"
#include "core/bar/bar_window.h"
#include "core/bar/screens.h"
#include "core/config/loader.h"
#include "core/config/paths.h"
#include "core/css/preprocessor.h"
#include "core/css/transition_engine.h"
#include "core/widgets/widget_builder.h"
#include "core/version.h"
#include "widgets/stat_widgets.h"

namespace {

bool hasFlag(int argc, char** argv, const char* flag) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) {
      return true;
    }
  }
  return false;
}

// The exe uses the GUI subsystem: write to a redirected stdout, else attach to the parent console.
void printVersion() {
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out == nullptr || out == INVALID_HANDLE_VALUE) {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
      return;
    }
    out = GetStdHandle(STD_OUTPUT_HANDLE);
  }
  const std::string text = std::string("yasb-native ") + yasb::version() + "\r\n";
  DWORD written = 0;
  WriteFile(out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

// Mirrors Python YASB (src/env.py): GDI by default. Qt's DirectWrite font database memory-maps
// every installed font file, which costs ~1.7k handles and ~100 MiB working set on large font sets.
void setFontEngine() {
  std::string engine = "gdi";
  if (const QByteArray requested = qgetenv("YASB_FONT_ENGINE").toLower();
      requested == "native" || requested == "freetype" || requested == "gdi") {
    engine = requested.toStdString();
  }
  qputenv("QT_QPA_PLATFORM", QByteArray("windows:fontengine=") + engine.c_str());
}

const char* optionValue(int argc, char** argv, const char* flag) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
  }
  return nullptr;
}

// Builds the first configured bar. App-bar registration is opt-in so --config-home runs never
// disturb the live desktop's work area.
// --screenshot <png> renders the bar to a file without ever showing it.
int runBar(int argc, char** argv) {
  const auto result = yasb::config::loadConfigFile(yasb::config::configPath());
  if (!result.ok()) {
    qCritical().noquote() << "Config not loaded:" << QString::fromStdString(result.message);
    return 1;
  }

  yasb::css::CssPreprocessor pre(QString::fromStdWString(yasb::config::stylesheetPath().wstring()));
  static yasb::css::TransitionEngine engine;
  const QString stylesheet = engine.loadStyleSheet(pre.process());
  engine.install(qApp);
  const bool screenshot = optionValue(argc, argv, "--screenshot") != nullptr;

  std::unique_ptr<yasb::app::AppServices> services;
  if (!screenshot) {
    services = std::make_unique<yasb::app::AppServices>(result.config);
    if (!services->start(hasFlag(argc, argv, "--restart-wait"))) {
      qCritical().noquote() << "Application services not started:" << QString::fromStdString(services->error());
      return 1;
    }
  }

  std::vector<yasb::bar::BarScreens> bars;
  for (const auto& [name, bar] : result.config.at("bars").items()) {
    if (!bar.value("enabled", true)) continue;
    bars.push_back({name, bar.at("screens").get<std::vector<std::string>>()});
  }
  std::vector<std::string> screenNames;
  for (QScreen* s : QGuiApplication::screens()) screenNames.push_back(s->name().toStdString());
  QScreen* primary = QGuiApplication::primaryScreen();
  const auto assignments = yasb::bar::resolveBarScreens(bars, screenNames, primary->name().toStdString());
  if (assignments.empty()) {
    qCritical() << "No enabled bar matches a connected screen";
    return 1;
  }

  const auto& pick = assignments.front();
  QScreen* screen = yasb::bar::screenByName(QGuiApplication::screens(), QString::fromStdString(pick.screenName));
  const QString barId = QStringLiteral("%1_%2").arg(QString::fromStdString(pick.barName), screen->name());
  yasb::bar::BarDeps deps;
  if (hasFlag(argc, argv, "--register-app-bar")) deps.appBar = yasb::bar::makeWin32AppBar();

  yasb::widgets::registerStatWidgets();
  yasb::widgets::WidgetBuilder::Columns columns;
  const auto& barConfig = result.config["bars"][pick.barName];
  if (const auto placements = barConfig.find("widgets"); placements != barConfig.end() && placements->is_object()) {
    for (const char* side : {"left", "center", "right"}) {
      const auto items = placements->find(side);
      if (items == placements->end() || !items->is_array()) continue;
      auto& column = columns[QString::fromLatin1(side)];
      for (const auto& item : *items) {
        if (item.is_string()) column.append(QString::fromStdString(item.get<std::string>()));
      }
    }
  }
  yasb::widgets::WidgetBuilder builder(result);
  const auto widgets = builder.buildWidgets(columns, screen->name(), barId);
  auto* window = new yasb::bar::BarWindow(barId, QString::fromStdString(pick.barName), screen, stylesheet, widgets,
                                          yasb::bar::BarSettings::fromJson(result.config["bars"][pick.barName]),
                                          std::move(deps));
  if (screenshot) {
    const char* png = optionValue(argc, argv, "--screenshot");
    window->ensurePolished();
    const QPixmap screenshotImage = window->barFrame()->grab();
    if (screenshotImage.isNull()) {
      qCritical().noquote() << "Failed to grab bar screenshot for" << QString::fromUtf8(png)
                            << "frame size:" << window->barFrame()->size();
      delete window;
      return 2;
    }
    if (!screenshotImage.save(QString::fromUtf8(png))) {
      qCritical().noquote() << "Failed to save bar screenshot:" << QString::fromUtf8(png);
      delete window;
      return 2;
    }
    delete window;
    return 0;
  }
  services->addBar(window);
  const QPointer<yasb::bar::BarWindow> bar(window);
  QObject::connect(services.get(), &yasb::app::AppServices::stylesheetChanged, qApp, [bar] {
    if (!bar) return;
    yasb::css::CssPreprocessor updated(QString::fromStdWString(yasb::config::stylesheetPath().wstring()));
    bar->setStyleSheet(engine.loadStyleSheet(updated.process()));
  });
  QObject::connect(services.get(), &yasb::app::AppServices::exitRequested, qApp, &QCoreApplication::quit);
  qApp->setQuitOnLastWindowClosed(false);
  window->show();
  qInfo().noquote() << "Bar" << QString::fromStdString(pick.barName) << "Created on Screen" << screen->name();
  return QApplication::exec();
}

}  // namespace

int main(int argc, char** argv) {
  winrt::init_apartment(winrt::apartment_type::single_threaded);

  if (hasFlag(argc, argv, "--version")) {
    printVersion();
    return 0;
  }

  setFontEngine();
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("yasb-native"));
  QCoreApplication::setApplicationVersion(QString::fromLatin1(yasb::version()));

  // --smoke: construct the application and exit without showing UI.
  if (hasFlag(argc, argv, "--smoke")) {
    return 0;
  }

  if (const char* configHome = optionValue(argc, argv, "--config-home")) {
    SetEnvironmentVariableW(L"YASB_CONFIG_HOME", QString::fromUtf8(configHome).toStdWString().c_str());
  }
  return runBar(argc, argv);
}
