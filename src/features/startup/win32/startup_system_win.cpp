#include "features/startup/win32/startup_system_win.h"

#include <windows.h>
#include <shlobj.h>
#include <taskschd.h>
#include <winrt/base.h>

#define SECURITY_WIN32
#include <security.h>

#include <QDir>
#include <QFileInfo>
#include <exception>
#include <string>

#include "kernel/win32/com_scope.h"

namespace waku::startup {
namespace {

constexpr wchar_t kTaskName[] = L"waku";
constexpr wchar_t kKeysTaskName[] = L"waku keys";
constexpr wchar_t kShortcutName[] = L"waku.lnk";
constexpr HRESULT kNotFound = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

// Owns a BSTR.
class Bstr {
 public:
  explicit Bstr(const std::wstring& text)
      : value_(SysAllocStringLen(text.data(), UINT(text.size()))) {}
  ~Bstr() { SysFreeString(value_); }
  Bstr(const Bstr&) = delete;
  Bstr& operator=(const Bstr&) = delete;
  BSTR get() const { return value_; }

 private:
  BSTR value_;
};

// Owns a VARIANT (empty by default).
class Variant {
 public:
  Variant() { VariantInit(&value_); }
  explicit Variant(const std::wstring& text) {
    VariantInit(&value_);
    value_.vt = VT_BSTR;
    value_.bstrVal = SysAllocStringLen(text.data(), UINT(text.size()));
  }
  ~Variant() { VariantClear(&value_); }
  Variant(const Variant&) = delete;
  Variant& operator=(const Variant&) = delete;
  const VARIANT& get() const { return value_; }

 private:
  VARIANT value_;
};

QString hresultText(const char* what, HRESULT hr) {
  const QString code =
      QString::number(quint32(hr), 16).toUpper().rightJustified(8, QLatin1Char('0'));
  if (hr == E_ACCESSDENIED) {
    return QStringLiteral("access was denied while trying to %1 (HRESULT 0x%2)")
        .arg(QString::fromLatin1(what), code);
  }
  return QStringLiteral("could not %1 (HRESULT 0x%2)").arg(QString::fromLatin1(what), code);
}

std::wstring xmlEscape(const std::wstring& text) {
  std::wstring out;
  for (wchar_t c : text) {
    switch (c) {
      case L'&':
        out += L"&amp;";
        break;
      case L'<':
        out += L"&lt;";
        break;
      case L'>':
        out += L"&gt;";
        break;
      case L'"':
        out += L"&quot;";
        break;
      case L'\'':
        out += L"&apos;";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

std::wstring currentUser() {
  ULONG size = 0;
  GetUserNameExW(NameSamCompatible, nullptr, &size);
  if (size == 0) return {};
  std::wstring name(size, L'\0');
  if (!GetUserNameExW(NameSamCompatible, name.data(), &size)) return {};
  name.resize(size);
  return name;
}

using Task = StartupSystem::Task;

const wchar_t* taskName(Task task) { return task == Task::Keys ? kKeysTaskName : kTaskName; }

std::wstring taskXml(Task task, const std::wstring& user, const std::wstring& arguments) {
  const std::wstring u = xmlEscape(user);
  const bool keys = task == Task::Keys;
  return std::wstring(
             L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
             L"<Task version=\"1.2\" "
             L"xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
             L"<RegistrationInfo><Description>") +
         (keys ? L"Starts the waku keys daemon at logon (wakuc startup enable)"
               : L"Starts the waku shell at logon (wakuc startup enable)") +
         L"</Description></RegistrationInfo>\n"
         L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" +
         u +
         L"</UserId></LogonTrigger></Triggers>\n"
         L"<Principals><Principal id=\"Author\"><UserId>" +
         u +
         L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel>"
         L"</Principal></Principals>\n"
         L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
         L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
         L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
         L"<AllowHardTerminate>true</AllowHardTerminate><StartWhenAvailable>true</"
         L"StartWhenAvailable>"
         L"<AllowStartOnDemand>true</AllowStartOnDemand><Enabled>true</Enabled><Hidden>false</"
         L"Hidden>"
         L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>4</Priority></Settings>\n"
         L"<Actions Context=\"Author\"><Exec><Command>" +
         std::wstring(keys ? L"wakukeys.exe" : L"waku.exe") + L"</Command><Arguments>" +
         xmlEscape(arguments) + L"</Arguments></Exec></Actions>\n</Task>\n";
}

// Connects to the Task Scheduler and opens the root folder.
HRESULT openRoot(winrt::com_ptr<ITaskFolder>& root) {
  winrt::com_ptr<ITaskService> service;
  HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(service.put()));
  if (FAILED(hr)) return hr;
  Variant empty;
  hr = service->Connect(empty.get(), empty.get(), empty.get(), empty.get());
  if (FAILED(hr)) return hr;
  Bstr path(L"\\");
  return service->GetFolder(path.get(), root.put());
}

QString startupFolder() {
  PWSTR raw = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &raw))) {
    CoTaskMemFree(raw);
    return {};
  }
  const QString folder = QString::fromWCharArray(raw);
  CoTaskMemFree(raw);
  return folder;
}

bool deleteInStartup(const wchar_t* name) {
  const QString folder = startupFolder();
  if (folder.isEmpty()) return false;
  const std::wstring path =
      QDir::toNativeSeparators(folder + QLatin1Char('/') + QString::fromWCharArray(name))
          .toStdWString();
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
  return DeleteFileW(path.c_str()) != 0;
}

class Win32StartupSystem final : public StartupSystem {
 public:
  QString findOnPath(const QString& exeName) const override {
    try {
      const std::wstring name = exeName.toStdWString();
      // Search the PATH folders only. With a null path SearchPathW looks in the folder of this
      // program first, which always holds waku.exe and would hide a missing PATH entry.
      std::wstring searchPath(32768, L'\0');
      const DWORD pathLength =
          GetEnvironmentVariableW(L"PATH", searchPath.data(), DWORD(searchPath.size()));
      if (pathLength == 0 || pathLength >= searchPath.size()) return {};
      searchPath.resize(pathLength);
      std::wstring buffer(MAX_PATH, L'\0');
      for (int attempt = 0; attempt < 4; ++attempt) {
        const DWORD n = SearchPathW(searchPath.c_str(), name.c_str(), nullptr, DWORD(buffer.size()),
                                    buffer.data(), nullptr);
        if (n == 0) return {};
        if (n < buffer.size()) {
          return QDir::toNativeSeparators(QString::fromWCharArray(buffer.c_str(), int(n)));
        }
        buffer.assign(n + 1, L'\0');
      }
    } catch (const std::exception&) {
    }
    return {};
  }

  QString ownFolder() const override {
    try {
      std::wstring buffer(MAX_PATH, L'\0');
      for (int attempt = 0; attempt < 8; ++attempt) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
        if (n == 0) return {};
        if (n < buffer.size()) {
          const QString file = QString::fromWCharArray(buffer.c_str(), int(n));
          return QDir::toNativeSeparators(QFileInfo(file).absolutePath());
        }
        buffer.assign(buffer.size() * 2, L'\0');
      }
    } catch (const std::exception&) {
    }
    return {};
  }

  QString keysConfigPath() const override {
    return QDir::toNativeSeparators(QDir::homePath() + QStringLiteral("/.config/waku/keys.conf"));
  }

  bool keysConfigExists() const override { return QFileInfo(keysConfigPath()).isFile(); }

  TaskInfo queryTask(Task which) const override {
    TaskInfo info;
    try {
      kernel::ComScope com(kernel::ComApartment::SingleThreaded);
      winrt::com_ptr<ITaskFolder> root;
      if (FAILED(openRoot(root))) return info;
      Bstr name(taskName(which));
      winrt::com_ptr<IRegisteredTask> task;
      if (FAILED(root->GetTask(name.get(), task.put()))) return info;
      info.registered = true;
      VARIANT_BOOL enabled = VARIANT_FALSE;
      if (SUCCEEDED(task->get_Enabled(&enabled))) info.enabled = enabled != VARIANT_FALSE;
      winrt::com_ptr<ITaskDefinition> definition;
      winrt::com_ptr<IActionCollection> actions;
      winrt::com_ptr<IAction> action;
      if (FAILED(task->get_Definition(definition.put())) ||
          FAILED(definition->get_Actions(actions.put())) ||
          FAILED(actions->get_Item(1, action.put()))) {
        return info;
      }
      const auto exec = action.try_as<IExecAction>();
      BSTR arguments = nullptr;
      if (exec && SUCCEEDED(exec->get_Arguments(&arguments)) && arguments) {
        info.arguments = QString::fromWCharArray(arguments, int(SysStringLen(arguments)));
      }
      SysFreeString(arguments);
    } catch (const std::exception&) {
    }
    return info;
  }

  QString registerTask(Task which, const QString& arguments) override {
    try {
      const std::wstring user = currentUser();
      if (user.empty()) return QStringLiteral("could not read the current user name");
      kernel::ComScope com(kernel::ComApartment::SingleThreaded);
      winrt::com_ptr<ITaskFolder> root;
      HRESULT hr = openRoot(root);
      if (FAILED(hr)) return hresultText("connect to the Task Scheduler", hr);
      Bstr name(taskName(which));
      Bstr xml(taskXml(which, user, arguments.toStdWString()));
      Variant userId(user);
      Variant empty;
      winrt::com_ptr<IRegisteredTask> task;
      hr = root->RegisterTask(name.get(), xml.get(), TASK_CREATE_OR_UPDATE, userId.get(),
                              empty.get(), TASK_LOGON_INTERACTIVE_TOKEN, empty.get(), task.put());
      if (FAILED(hr)) return hresultText("register the task", hr);
      return {};
    } catch (const std::exception& e) {
      return QStringLiteral("could not register the task (%1)").arg(QString::fromUtf8(e.what()));
    }
  }

  QString removeTask(Task which) override {
    try {
      kernel::ComScope com(kernel::ComApartment::SingleThreaded);
      winrt::com_ptr<ITaskFolder> root;
      HRESULT hr = openRoot(root);
      if (FAILED(hr)) return hresultText("connect to the Task Scheduler", hr);
      Bstr name(taskName(which));
      hr = root->DeleteTask(name.get(), 0);
      if (FAILED(hr) && hr != kNotFound) return hresultText("remove the task", hr);
      return {};
    } catch (const std::exception& e) {
      return QStringLiteral("could not remove the task (%1)").arg(QString::fromUtf8(e.what()));
    }
  }

  bool removeShortcut() override {
    try {
      kernel::ComScope com(kernel::ComApartment::SingleThreaded);
      return deleteInStartup(kShortcutName);
    } catch (const std::exception&) {
      return false;
    }
  }
};

}  // namespace

std::unique_ptr<StartupSystem> makeWin32StartupSystem() {
  return std::make_unique<Win32StartupSystem>();
}

}  // namespace waku::startup
