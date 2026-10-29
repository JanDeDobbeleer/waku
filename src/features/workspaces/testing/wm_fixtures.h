#pragma once

// Fixtures shared by the komorebi, GlazeWM and Workspaces tests: sample window-manager state and
// harnesses that build the backends on their services' test seams.

#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "features/workspaces/qml/glazewm_client.h"
#include "features/workspaces/qml/komorebi_client.h"

using nlohmann::json;
using namespace wabar::workspaces;
namespace gw = wabar::workspaces::glazewm;
namespace kmb = wabar::workspaces::komorebi;

namespace {

// ---- GlazeWM ----

json window(const std::string& id) {
  return {{"type", "window"},
          {"id", id},
          {"title", "t"},
          {"handle", 4242},
          {"className", "c"},
          {"processName", "p"},
          {"displayState", "shown"},
          {"state", {{"type", "tiling"}}}};
}

json workspace(const std::string& name, bool focus, bool displayed, json children) {
  return {{"type", "workspace"},       {"name", name},
          {"displayName", name + "!"}, {"hasFocus", focus},
          {"isDisplayed", displayed},  {"children", std::move(children)}};
}

json monitorsData() {
  json mon = {{"type", "monitor"},
              {"hardwareId", "DEL1234"},
              {"handle", 65537},
              {"children", json::array({workspace("1", true, true, json::array({window("a")})),
                                        workspace("2", false, false, json::array())})}};
  return {{"monitors", json::array({mon})}};
}

json response(const std::string& clientMessage, json data) {
  return {{"messageType", "client_response"},
          {"clientMessage", clientMessage},
          {"success", true},
          {"data", std::move(data)}};
}

struct GlazeHarness {
  std::vector<std::string> sent;
  std::vector<QString> opened;
  std::unique_ptr<GlazeWM> glaze;

  GlazeHarness() {
    glaze = std::make_unique<GlazeWM>(
        [this](const QByteArray& bytes) {
          sent.push_back(bytes.toStdString());
          return true;
        },
        [this](const QString& uri) { opened.push_back(uri); }, nullptr);
  }

  void message(const json& value) {
    glaze->service().handleMessage(QString::fromStdString(value.dump()));
  }
};

// ---- komorebi ----

json komorebiWorkspace(const std::string& name, int windows) {
  json containers = json::array();
  for (int i = 0; i < windows; ++i) {
    containers.push_back({{"windows", {{"elements", json::array({json{{"hwnd", 100 + i}}})}}}});
  }
  return {{"name", name},
          {"containers", {{"elements", containers}}},
          {"monocle_container", nullptr},
          {"maximized_window", nullptr},
          {"floating_windows", {{"elements", json::array()}}}};
}

// Two monitors; the first has three workspaces (I, II, unnamed) with `focusedWorkspace` shown, the
// second has one. `focusedMonitor` is the focused monitor index.
kmb::JsonPtr komorebiState(int focusedWorkspace = 1, int focusedMonitor = 0) {
  json screen = {{"id", 65537},
                 {"name", "DISPLAY1"},
                 {"workspaces",
                  {{"elements", json::array({komorebiWorkspace("I", 0), komorebiWorkspace("II", 2),
                                             komorebiWorkspace("", 1)})},
                   {"focused", focusedWorkspace}}}};
  json second = {
      {"id", 131073},
      {"name", "DISPLAY2"},
      {"workspaces", {{"elements", json::array({komorebiWorkspace("A", 0)})}, {"focused", 0}}}};
  return std::make_shared<const json>(json{
      {"monitors", {{"elements", json::array({screen, second})}, {"focused", focusedMonitor}}}});
}

struct KomorebiHarness {
  std::vector<std::vector<QStringList>> runs;
  std::unique_ptr<Komorebi> komorebi;

  KomorebiHarness() {
    kmb::KomorebiBackend backend;
    backend.isKomorebiRunning = [] { return false; };  // the listener thread stays idle
    backend.runKomorebic = [](const QStringList&, int) { return kmb::KomorebicResult{}; };
    komorebi = std::make_unique<Komorebi>(
        backend, [this](const std::vector<QStringList>& commands) { runs.push_back(commands); },
        QStringLiteral("yasb-native-services-test"), kmb::KomorebiPipeListener::Timing{}, nullptr);
  }

  void publish(const QString& kind, const kmb::JsonPtr& state = nullptr) {
    QVariantMap payload{{QStringLiteral("kind"), kind}};
    if (state) payload.insert(QStringLiteral("state"), QVariant::fromValue(state));
    emit komorebi->bus().event(kmb::kStateEvent, payload);
  }
};

}  // namespace
