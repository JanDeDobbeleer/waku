#pragma once

#include <QScreen>
#include <QString>
#include <cstdint>
#include <memory>
#include <vector>

namespace yasb::bar {

enum class AppBarEdge { Top = 1, Bottom = 3 };

constexpr unsigned kAppBarCallbackMessage = 0x0400 + 100;  // WM_USER + 100

struct AppBarRequest {
  std::uintptr_t hwnd = 0;
  AppBarEdge edge = AppBarEdge::Top;
  int height = 0;  // dimensions.height + padding.top + padding.bottom, logical pixels
  QScreen* screen = nullptr;
  bool scaleScreenHeight = false;
  QString barName;
  bool reserveSpace = false;
  bool alwaysOnTop = false;
};

// The RECT SHAppBarMessage is asked to reserve (pure, testable; port of Win32AppBar.position_bar).
struct AppBarRect {
  int left = 0, top = 0, right = 0, bottom = 0;
  bool operator==(const AppBarRect&) const = default;
};
AppBarRect computeAppBarRect(AppBarEdge edge, int height, const QRect& screenGeometry, double dpr, bool scaleScreenHeight);

// Seam so tests and --config-home runs never touch the live desktop's app-bar list.
class IAppBar {
 public:
  virtual ~IAppBar() = default;
  virtual void create(const AppBarRequest& request) = 0;
  virtual void remove() = 0;
};

// Records requests; registers nothing.
class FakeAppBar : public IAppBar {
 public:
  void create(const AppBarRequest& request) override { requests.push_back(request); }
  void remove() override { ++removeCount; }
  std::vector<AppBarRequest> requests;
  int removeCount = 0;
};

// Talks to SHAppBarMessage and sets WS_EX_NOACTIVATE / WS_EX_TOPMOST. Live desktop only.
std::unique_ptr<IAppBar> makeWin32AppBar();

}  // namespace yasb::bar
