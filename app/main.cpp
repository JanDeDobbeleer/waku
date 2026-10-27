#include <windows.h>

#include <winrt/base.h>

#include <QApplication>
#include <QCoreApplication>
#include <QString>

#include <cstring>
#include <string>

#include "core/version.h"

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

}  // namespace

int main(int argc, char** argv) {
  winrt::init_apartment(winrt::apartment_type::single_threaded);

  if (hasFlag(argc, argv, "--version")) {
    printVersion();
    return 0;
  }

  QApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("yasb-native"));
  QCoreApplication::setApplicationVersion(QString::fromLatin1(yasb::version()));

  // --smoke: construct the application and exit without showing UI.
  if (hasFlag(argc, argv, "--smoke")) {
    return 0;
  }

  return 0;
}
