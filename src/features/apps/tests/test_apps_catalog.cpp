#include <windows.h>
#include <gtest/gtest.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl_core.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "features/apps/apps_platform.h"
#include "features/apps/catalog.h"

using namespace waku::apps;

namespace {

QString s(const char* text) { return QString::fromLatin1(text); }

void touch(const QString& path) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("x");
}

// A resolver that never reads the file: every .lnk "targets" C:\apps\<name>.exe.
ShortcutResolver fakeResolver() {
  return [](const QString& path) {
    ShortcutInfo info;
    info.ok = !path.contains(QLatin1String("broken"));
    info.target =
        QStringLiteral("C:\\apps\\") + QFileInfo(path).completeBaseName() + QStringLiteral(".exe");
    info.arguments = QStringLiteral("--flag \"two words\"");
    info.workingDirectory = QStringLiteral("C:\\apps");
    info.description = QStringLiteral("About ") + QFileInfo(path).completeBaseName();
    return info;
  };
}

AppRecord store(const char* name, const char* aumid) {
  AppRecord record;
  record.id = s(aumid);
  record.name = s(name);
  record.kind = AppKind::Store;
  record.aumid = s(aumid);
  record.iconSource = QStringLiteral("shell:AppsFolder\\") + s(aumid);
  return record;
}

}  // namespace

TEST(AppsCatalog, NoiseNames) {
  EXPECT_TRUE(isNoiseName(s("Uninstall Foo")));
  EXPECT_TRUE(isNoiseName(s("Foo UNINSTALLER")));
  EXPECT_TRUE(isNoiseName(s("ReadMe")));
  EXPECT_TRUE(isNoiseName(s("Foo Help")));
  EXPECT_TRUE(isNoiseName(s("Help and Support")));
  EXPECT_FALSE(isNoiseName(s("Helper Tool")));
  EXPECT_FALSE(isNoiseName(s("Notepad")));
}

TEST(AppsCatalog, ScansShortcutsRecursivelyAndSkipsNoise) {
  QTemporaryDir user;
  QTemporaryDir common;
  touch(user.filePath(s("Alpha.lnk")));
  touch(user.filePath(s("Dev Tools/Beta.lnk")));
  touch(user.filePath(s("Dev Tools/Uninstall Beta.lnk")));
  touch(user.filePath(s("Dev Tools/readme.lnk")));
  touch(user.filePath(s("Dev Tools/Beta Help.lnk")));
  touch(user.filePath(s("notes.txt")));
  touch(user.filePath(s("broken.lnk")));
  touch(common.filePath(s("Alpha.lnk")));  // same id: the user's wins
  touch(common.filePath(s("Gamma.lnk")));
  const auto records = scanShortcuts({user.path(), common.path()}, fakeResolver());
  QStringList ids;
  for (const AppRecord& r : records) ids.append(r.id);
  ids.sort();
  EXPECT_EQ(ids, (QStringList{s("Alpha"), s("Dev Tools/Beta"), s("Gamma")}));

  const AppRecord* beta = nullptr;
  for (const AppRecord& r : records) {
    if (r.name == s("Beta")) beta = &r;
  }
  ASSERT_TRUE(beta);
  EXPECT_EQ(beta->kind, AppKind::Shortcut);
  EXPECT_EQ(beta->categories, (QStringList{s("Dev Tools")}));
  EXPECT_EQ(beta->target, s("C:\\apps\\Beta.exe"));
  EXPECT_EQ(beta->comment, s("About Beta"));
  EXPECT_EQ(beta->keywords, (QStringList{s("Beta")}));
  EXPECT_EQ(beta->iconSource, beta->path);
  EXPECT_TRUE(beta->path.endsWith(s("Beta.lnk")));
  EXPECT_EQ(startupClassFor(*beta), s("Beta"));
}

TEST(AppsCatalog, ScanIgnoresMissingFoldersAndANullResolver) {
  EXPECT_TRUE(scanShortcuts({s("C:\\waku-no-such-folder")}, fakeResolver()).empty());
  QTemporaryDir dir;
  touch(dir.filePath(s("A.lnk")));
  EXPECT_TRUE(scanShortcuts({dir.path()}, ShortcutResolver()).empty());
}

TEST(AppsCatalog, BuildSortsAndDedupes) {
  QTemporaryDir dir;
  touch(dir.filePath(s("zed.lnk")));
  touch(dir.filePath(s("Calculator.lnk")));
  touch(dir.filePath(s("alpha.lnk")));
  auto shortcuts = scanShortcuts({dir.path()}, fakeResolver());
  std::vector<AppRecord> apps = {
      store("Photos", "Microsoft.Photos_8wekyb3d8bbwe!App"),
      store("Calculator", "Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"),  // shortcut has it
      store("Photos Again", "Microsoft.Photos_8wekyb3d8bbwe!App"),           // repeated id
      store("", "Nameless_x!App"),
      store("Store Help", "Help_x!App"),
  };
  const auto catalog = buildCatalog(std::move(shortcuts), std::move(apps));
  QStringList names;
  for (const AppRecord& r : catalog) names.append(r.name);
  EXPECT_EQ(names, (QStringList{s("alpha"), s("Calculator"), s("Photos"), s("zed")}));
}

TEST(AppsCatalog, WatchedDirectoriesListsTheTree) {
  QTemporaryDir dir;
  touch(dir.filePath(s("a/b/c.lnk")));
  touch(dir.filePath(s("d.lnk")));
  const QStringList watched = watchedDirectories({dir.path(), s("C:\\waku-no-such-folder")});
  EXPECT_EQ(watched.size(), 3);  // root, a, a/b
}

TEST(AppsCatalog, CommandAndExecString) {
  AppRecord shortcut;
  shortcut.kind = AppKind::Shortcut;
  shortcut.path = s("C:\\menu\\App.lnk");
  shortcut.target = s("C:\\Program Files\\App\\app.exe");
  shortcut.arguments = s("--flag \"two words\"");
  EXPECT_EQ(execStringFor(shortcut), s("\"C:\\Program Files\\App\\app.exe\" --flag \"two words\""));
  EXPECT_EQ(commandFor(shortcut),
            (QStringList{s("C:\\Program Files\\App\\app.exe"), s("--flag"), s("two words")}));
  EXPECT_EQ(startupClassFor(shortcut), s("app"));

  shortcut.target.clear();
  EXPECT_EQ(commandFor(shortcut), (QStringList{s("C:\\menu\\App.lnk")}));
  EXPECT_EQ(startupClassFor(shortcut), QString());

  const AppRecord packaged = store("Photos", "Microsoft.Photos_8wekyb3d8bbwe!App");
  EXPECT_EQ(execStringFor(packaged), s("shell:AppsFolder\\Microsoft.Photos_8wekyb3d8bbwe!App"));
  EXPECT_EQ(
      commandFor(packaged),
      (QStringList{s("explorer.exe"), s("shell:AppsFolder\\Microsoft.Photos_8wekyb3d8bbwe!App")}));
}

// The real resolver reads a real .lnk written with IShellLink into a temp folder.
TEST(AppsCatalog, RealResolverReadsAShortcutFile) {
  QTemporaryDir dir;
  const QString lnk = QDir::toNativeSeparators(dir.filePath(s("Sample App.lnk")));
  const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  {
    IShellLinkW* link = nullptr;
    ASSERT_TRUE(SUCCEEDED(
        CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))));
    link->SetPath(L"C:\\Windows\\notepad.exe");
    link->SetArguments(L"/a \"b c\"");
    link->SetWorkingDirectory(L"C:\\Windows");
    link->SetDescription(L"A sample");
    IPersistFile* file = nullptr;
    ASSERT_TRUE(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))));
    ASSERT_TRUE(SUCCEEDED(file->Save(reinterpret_cast<LPCWSTR>(lnk.utf16()), TRUE)));
    file->Release();
    link->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();

  const ShortcutInfo info = makeWin32ShortcutResolver()(lnk);
  ASSERT_TRUE(info.ok);
  EXPECT_EQ(info.target.toLower(), s("c:\\windows\\notepad.exe"));
  EXPECT_EQ(info.arguments, s("/a \"b c\""));
  EXPECT_EQ(info.workingDirectory, s("C:\\Windows"));
  EXPECT_EQ(info.description, s("A sample"));

  // Scanning that folder with the real resolver finds the app.
  const auto records = scanShortcuts({dir.path()}, makeWin32ShortcutResolver());
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records[0].id, s("Sample App"));

  EXPECT_FALSE(makeWin32ShortcutResolver()(dir.filePath(s("missing.lnk"))).ok);

  // A hidden shortcut is not part of the menu.
  SetFileAttributesW(reinterpret_cast<LPCWSTR>(lnk.utf16()), FILE_ATTRIBUTE_HIDDEN);
  EXPECT_FALSE(makeWin32ShortcutResolver()(lnk).ok);
  EXPECT_TRUE(scanShortcuts({dir.path()}, makeWin32ShortcutResolver()).empty());
}

TEST(AppsCatalog, ExecutionAliasIsTheOnlyExeOfThePackageFolder) {
  QTemporaryDir dir;
  const QString family = s("Microsoft.WindowsTerminal_8wekyb3d8bbwe");
  QDir(dir.path()).mkpath(family);
  const QString aumid = family + s("!App");

  EXPECT_EQ(findExecutionAlias(dir.path(), aumid), QString());  // no alias
  touch(dir.filePath(family + s("/wt.exe")));
  EXPECT_EQ(findExecutionAlias(dir.path(), aumid), dir.filePath(family + s("/wt.exe")));
  touch(dir.filePath(family + s("/wtd.exe")));
  EXPECT_EQ(findExecutionAlias(dir.path(), aumid), QString());  // ambiguous
  EXPECT_EQ(findExecutionAlias(dir.path(), s("Unknown_1234!App")), QString());
  EXPECT_EQ(findExecutionAlias(QString(), aumid), QString());
}
