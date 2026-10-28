#include "platform/user_info.h"

#include <windows.h>
#include <sddl.h>
#define SECURITY_WIN32
#include <security.h>

#include <QFileInfo>
#include <QString>
#include <vector>

namespace yasb::platform {
namespace {

QString currentUserSid() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  QString result;
  if (size) {
    std::vector<BYTE> buffer(size);
    if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
      LPWSTR text = nullptr;
      if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text)) {
        result = QString::fromWCharArray(text);
        LocalFree(text);
      }
    }
  }
  CloseHandle(token);
  return result;
}

QString computeAvatarPath() {
  const QString sid = currentUserSid();
  if (!sid.isEmpty()) {
    const std::wstring key =
        (QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AccountPicture\\Users\\") +
         sid)
            .toStdWString();
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &handle) == ERROR_SUCCESS) {
      QString best;
      int bestResolution = 0;
      for (DWORD index = 0;; ++index) {
        wchar_t name[256] = {};
        DWORD nameSize = 256;
        wchar_t data[1024] = {};
        DWORD dataSize = sizeof(data) - sizeof(wchar_t);
        DWORD type = 0;
        const LONG status = RegEnumValueW(handle, index, name, &nameSize, nullptr, &type,
                                          reinterpret_cast<BYTE*>(data), &dataSize);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS || type != REG_SZ) continue;
        const QString valueName = QString::fromWCharArray(name, static_cast<int>(nameSize));
        if (!valueName.startsWith(QStringLiteral("Image"))) continue;
        const QString path = QString::fromWCharArray(data);
        if (!QFileInfo(path).isFile()) continue;
        bool ok = false;
        const int resolution = valueName.mid(5).toInt(&ok);
        if (ok && resolution > bestResolution) {
          bestResolution = resolution;
          best = path;
        }
      }
      RegCloseKey(handle);
      if (!best.isEmpty()) return best;
    }
  }
  QString programData = qEnvironmentVariable("ProgramData");
  if (programData.isEmpty()) programData = QStringLiteral("C:\\ProgramData");
  const QString fallback =
      programData + QStringLiteral("\\Microsoft\\User Account Pictures\\user-192.png");
  return QFileInfo(fallback).isFile() ? fallback : QString();
}

QString computeUsername() {
  wchar_t name[256] = {};
  ULONG size = 256;
  if (GetUserNameExW(NameDisplay, name, &size) && name[0]) return QString::fromWCharArray(name);
  DWORD accountSize = 256;
  if (GetUserNameW(name, &accountSize)) return QString::fromWCharArray(name);
  return {};
}

}  // namespace

QString windowsUsername() {
  static const QString name = computeUsername();
  return name;
}

QString userAvatarPath() {
  static const QString path = computeAvatarPath();
  return path;
}

}  // namespace yasb::platform
