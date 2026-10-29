#include <windows.h>
#include <crtdbg.h>
#include <gtest/gtest.h>

#include <QGuiApplication>
#include <memory>

namespace {

// QGuiApplication on the offscreen platform with the software scene graph: the bar tests build
// real QQuickWindows without a display or a GPU.
class QtEnvironment final : public testing::Environment {
 public:
  void SetUp() override {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QML_DISABLE_DISK_CACHE", "1");  // as the app: edits reload from the source file
    app_ = std::make_unique<QGuiApplication>(argc_, argv_);
  }
  void TearDown() override { app_.reset(); }

 private:
  int argc_ = 1;
  char name_[24] = "wabar_services_tests";
  char* argv_[2] = {name_, nullptr};
  std::unique_ptr<QGuiApplication> app_;
};

}  // namespace

int main(int argc, char** argv) {
  // Keep fatal test errors in CTest output rather than interactive dialogs.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_error_mode(_OUT_TO_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  qputenv("QT_FORCE_STDERR_LOGGING", "1");
  testing::InitGoogleTest(&argc, argv);
  testing::AddGlobalTestEnvironment(new QtEnvironment);
  return RUN_ALL_TESTS();
}
