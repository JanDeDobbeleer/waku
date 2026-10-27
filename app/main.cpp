#include <windows.h>
#include <winrt/base.h>

#include <QApplication>
#include <QCoreApplication>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <cstdlib>
#include <cstring>
#include <exception>
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
#include "core/version.h"
#include "core/widgets/widget_builder.h"
#include "widgets/stat_widgets.h"

namespace {

// Process exit codes. 1 is a config or service start failure, 2 a --screenshot failure.
constexpr int kExitNoScreen = 3;
constexpr int kExitUnhandledException = 4;

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

std::vector<std::string> stringList(const nlohmann::json& object, const char* key) {
  std::vector<std::string> out;
  const auto it = object.find(key);
  if (it == object.end() || !it->is_array()) return out;
  for (const auto& item : *it) {
    if (item.is_string()) out.push_back(item.get<std::string>());
  }
  return out;
}

// Builds the first configured bar. App-bar registration is opt-in so --config-home runs never
// disturb the live desktop's work area.
// --screenshot <png> renders the bar to a file without ever showing it.
// `engine` is owned by main() so it is destroyed before QApplication.
int runBar(int argc, char** argv, yasb::css::TransitionEngine& engine) {
  const auto result = yasb::config::loadConfigFile(yasb::config::configPath());
  if (!result.ok()) {
    qCritical().noquote() << "Config not loaded:" << QString::fromStdString(result.message);
    return 1;
  }

  yasb::css::CssPreprocessor pre(QString::fromStdWString(yasb::config::stylesheetPath().wstring()));
  const QString stylesheet = engine.loadStyleSheet(pre.process());
  engine.install(qApp);
  const bool screenshot = optionValue(argc, argv, "--screenshot") != nullptr;

  std::unique_ptr<yasb::app::AppServices> services;
  if (!screenshot) {
    services = std::make_unique<yasb::app::AppServices>(result.config);
    if (!services->start(hasFlag(argc, argv, "--restart-wait"))) {
      qCritical().noquote() << "Application services not started:"
                            << QString::fromStdString(services->error());
      return 1;
    }
  }

  const nlohmann::json noBars = nlohmann::json::object();
  const auto barsIt = result.config.find("bars");
  const nlohmann::json& barsConfig =
      barsIt != result.config.end() && barsIt->is_object() ? *barsIt : noBars;
  std::vector<yasb::bar::BarScreens> bars;
  for (const auto& [name, bar] : barsConfig.items()) {
    if (!bar.is_object() || !bar.value("enabled", true)) continue;
    bars.push_back({name, stringList(bar, "screens")});
  }
  QScreen* primary = QGuiApplication::primaryScreen();
  if (primary == nullptr) {
    qCritical() << "No primary screen is available; cannot place a bar";
    return kExitNoScreen;
  }
  std::vector<std::string> screenNames;
  for (QScreen* s : QGuiApplication::screens()) screenNames.push_back(s->name().toStdString());
  const auto assignments =
      yasb::bar::resolveBarScreens(bars, screenNames, primary->name().toStdString());
  if (assignments.empty()) {
    qCritical() << "No enabled bar matches a connected screen";
    return 1;
  }

  const auto& pick = assignments.front();
  QScreen* screen =
      yasb::bar::screenByName(QGuiApplication::screens(), QString::fromStdString(pick.screenName));
  if (screen == nullptr) {
    qCritical().noquote() << "Screen" << QString::fromStdString(pick.screenName)
                          << "is no longer connected; cannot place bar"
                          << QString::fromStdString(pick.barName);
    return kExitNoScreen;
  }
  const QString barId =
      QStringLiteral("%1_%2").arg(QString::fromStdString(pick.barName), screen->name());
  yasb::bar::BarDeps deps;
  if (hasFlag(argc, argv, "--register-app-bar")) deps.appBar = yasb::bar::makeWin32AppBar();

  yasb::widgets::registerStatWidgets();
  yasb::widgets::WidgetBuilder::Columns columns;
  const auto barIt = barsConfig.find(pick.barName);
  const nlohmann::json& barConfig = barIt != barsConfig.end() ? *barIt : noBars;
  if (const auto placements = barConfig.find("widgets");
      placements != barConfig.end() && placements->is_object()) {
    for (const char* side : {"left", "center", "right"}) {
      auto& column = columns[QString::fromLatin1(side)];
      for (const auto& name : stringList(*placements, side))
        column.append(QString::fromStdString(name));
    }
  }
  yasb::widgets::WidgetBuilder builder(result);
  const auto widgets = builder.buildWidgets(columns, screen->name(), barId);
  auto* window = new yasb::bar::BarWindow(
      barId, QString::fromStdString(pick.barName), screen, stylesheet, widgets,
      yasb::bar::BarSettings::fromJson(barConfig), std::move(deps));
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
  QObject::connect(
      services.get(), &yasb::app::AppServices::stylesheetChanged, qApp, [bar, &engine] {
        if (!bar) return;
        try {
          yasb::css::CssPreprocessor updated(
              QString::fromStdWString(yasb::config::stylesheetPath().wstring()));
          bar->setStyleSheet(engine.loadStyleSheet(updated.process()));
        } catch (const std::exception& error) {
          qCritical().noquote() << "Stylesheet reload failed:" << QString::fromUtf8(error.what());
        }
      });
  QObject::connect(services.get(), &yasb::app::AppServices::exitRequested, qApp,
                   &QCoreApplication::quit);
  qApp->setQuitOnLastWindowClosed(false);
  window->show();
  qInfo().noquote() << "Bar" << QString::fromStdString(pick.barName) << "Created on Screen"
                    << screen->name();
  const int code = QApplication::exec();
  // Widgets go before the services and the engine that styles them.
  delete bar.data();
  return code;
}

// Last resort for exceptions that hit a noexcept boundary (for example a Qt callback): leave a line
// in the application log before the process dies.
[[noreturn]] void logTerminate() {
  qCritical() << "Terminating on an unhandled exception";
  std::abort();
}

}  // namespace

int main(int argc, char** argv) {
  winrt::init_apartment(winrt::apartment_type::single_threaded);

  if (hasFlag(argc, argv, "--version")) {
    printVersion();
    return 0;
  }

  setFontEngine();
  std::set_terminate(logTerminate);
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("yasb-native"));
  QCoreApplication::setApplicationVersion(QString::fromLatin1(yasb::version()));

  // --smoke: construct the application and exit without showing UI.
  if (hasFlag(argc, argv, "--smoke")) {
    return 0;
  }

  if (const char* configHome = optionValue(argc, argv, "--config-home")) {
    SetEnvironmentVariableW(L"YASB_CONFIG_HOME",
                            QString::fromUtf8(configHome).toStdWString().c_str());
  }
  try {
    yasb::css::TransitionEngine engine;
    return runBar(argc, argv, engine);
  } catch (const std::exception& error) {
    qCritical().noquote() << "Unhandled exception:" << QString::fromUtf8(error.what());
  } catch (...) {
    qCritical() << "Unhandled non-standard exception";
  }
  return kExitUnhandledException;
}
