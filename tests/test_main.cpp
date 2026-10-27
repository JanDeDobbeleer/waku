#include <gtest/gtest.h>

#include <QApplication>
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
  testing::InitGoogleTest(&argc, argv);
  testing::AddGlobalTestEnvironment(new QtEnvironment);
  return RUN_ALL_TESTS();
}
