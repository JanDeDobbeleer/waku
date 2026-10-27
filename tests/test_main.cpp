#include <windows.h>
#include <crtdbg.h>
#include <gtest/gtest.h>

#include <QApplication>
#include <cstdlib>
#include <memory>

namespace {

class QtEnvironment final : public testing::Environment {
 public:
  void SetUp() override {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    app_ = std::make_unique<QApplication>(argc_, argv_);
  }

  void TearDown() override { app_.reset(); }

 private:
  int argc_ = 1;
  char name_[11] = "yasb_tests";
  char* argv_[2] = {name_, nullptr};
  std::unique_ptr<QApplication> app_;
};

}  // namespace

int main(int argc, char** argv) {
  // Keep fatal test errors in CTest output rather than interactive CRT/Windows dialogs.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  _set_error_mode(_OUT_TO_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
  qputenv("QT_FORCE_STDERR_LOGGING", "1");
  testing::InitGoogleTest(&argc, argv);
  testing::AddGlobalTestEnvironment(new QtEnvironment);
  return RUN_ALL_TESTS();
}
