// User-built QML plugins: <config home>/modules and --import-path folders reach the engine's
// import path, and the guard refuses a plugin built for another Qt, flavour or processor before
// anything is loaded. The plugin is tests/plugin (WakuTestModule).
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <functional>
#include <memory>

#include "host/plugin_guard.h"

using namespace waku::host;

namespace {

QString builtModuleDir() {
  return QDir(QCoreApplication::applicationDirPath())
      .filePath(QStringLiteral("testmodules/WakuTestModule"));
}

// A modules folder holding a copy of the test plugin.
bool copyModule(const QString& modulesDir) {
  const QString target = QDir(modulesDir).filePath(QStringLiteral("WakuTestModule"));
  if (!QDir().mkpath(target)) return false;
  bool any = false;
  QDirIterator it(builtModuleDir(),
                  {QStringLiteral("qmldir"), QStringLiteral("*.dll"), QStringLiteral("*.qmltypes")},
                  QDir::Files);
  while (it.hasNext()) {
    it.next();
    const QString to = QDir(target).filePath(it.fileName());
    QFile::remove(to);
    if (!QFile::copy(it.filePath(), to)) return false;
    any = true;
  }
  return any;
}

QString pluginFileIn(const QString& modulesDir) {
  const QStringList files = findPluginFiles(modulesDir);
  return files.isEmpty() ? QString() : files.first();
}

// Changes bytes of a file at `offset` (found by `find`) and writes it back.
bool patchFile(const QString& path, const std::function<bool(QByteArray&)>& patch) {
  QFile file(path);
  if (!file.open(QIODevice::ReadWrite)) return false;
  QByteArray bytes = file.readAll();
  if (!patch(bytes)) return false;
  file.seek(0);
  return file.write(bytes) == bytes.size();
}

// The QML text of a component that creates the test type and reads its text.
QString greetingFrom(QQmlEngine& engine, QString* error) {
  QQmlComponent component(&engine);
  component.setData("import QtQml\nimport WakuTestModule\nTestGreeting {}\n",
                    QUrl(QStringLiteral("file:///plugin_user.qml")));
  if (component.isError()) {
    *error = component.errorString();
    return {};
  }
  std::unique_ptr<QObject> object(component.create());
  return object ? object->property("text").toString() : QString();
}

}  // namespace

// ---- the pure guard ----

TEST(PluginGuard, MetadataMustMatchQtMajorMinorAndFlavour) {
  const int qt = 0x060a02;
  const auto meta = [](int version, bool debug) {
    return QJsonObject{{QStringLiteral("version"), version}, {QStringLiteral("debug"), debug}};
  };
  EXPECT_TRUE(checkPluginMetadata(meta(0x060a02, false), qt, false).ok);
  EXPECT_TRUE(checkPluginMetadata(meta(0x060a00, true), qt, true).ok);  // patch level may differ

  const PluginVerdict minor = checkPluginMetadata(meta(0x060900, false), qt, false);
  EXPECT_FALSE(minor.ok);
  EXPECT_TRUE(minor.reason.contains(QStringLiteral("6.9.0"))) << qPrintable(minor.reason);
  EXPECT_FALSE(checkPluginMetadata(meta(0x050f00, false), qt, false).ok);
  EXPECT_FALSE(checkPluginMetadata(meta(0x060a02, true), qt, false).ok);
  EXPECT_FALSE(checkPluginMetadata(meta(0x060a02, false), qt, true).ok);
  EXPECT_FALSE(checkPluginMetadata({}, qt, false).ok);
  EXPECT_FALSE(
      checkPluginMetadata(QJsonObject{{QStringLiteral("version"), 0x060a02}}, qt, false).ok);
}

TEST(PluginGuard, ReadsTheMachineOfAPeHeader) {
  QByteArray header(0x100, '\0');
  header.replace(0, 2, QByteArray("MZ"));
  header.replace(0x3C, 1, QByteArray::fromHex("80"));
  header.replace(0x80, 4, QByteArray("PE\0\0", 4));
  header.replace(0x84, 2, QByteArray::fromHex("6486"));
  EXPECT_EQ(peMachine(header), 0x8664);
  header.replace(0x84, 2, QByteArray::fromHex("64aa"));
  EXPECT_EQ(peMachine(header), 0xAA64);
  EXPECT_EQ(peMachine(QByteArray("not a pe file")), 0);
  EXPECT_EQ(peMachine({}), 0);
}

TEST(PluginGuard, ImportRootsAreTheModulesFolderThenTheExtrasThatExist) {
  QTemporaryDir home;
  QTemporaryDir extra;
  ASSERT_TRUE(home.isValid() && extra.isValid());
  EXPECT_TRUE(importRoots(home.path(), {}).isEmpty());  // no modules folder yet
  ASSERT_TRUE(QDir(home.path()).mkpath(QStringLiteral("modules")));
  const QStringList roots = importRoots(
      home.path(), {extra.path(), extra.path() + QStringLiteral("/missing"), extra.path()});
  ASSERT_EQ(roots.size(), 2);
  EXPECT_TRUE(roots.first().endsWith(QStringLiteral("/modules")));
  EXPECT_EQ(QDir::cleanPath(roots.last()), QDir::cleanPath(extra.path()));
}

// ---- the real plugin ----

TEST(PluginImport, AModulesFolderPluginLoadsThroughTheEngineImportPath) {
  if (!QFile::exists(builtModuleDir())) GTEST_SKIP() << "the test plugin is not built";
  QTemporaryDir home;
  ASSERT_TRUE(home.isValid());
  ASSERT_TRUE(copyModule(QDir(home.path()).filePath(QStringLiteral("modules"))));

  const QStringList roots = importRoots(home.path(), {});
  ASSERT_EQ(roots.size(), 1);
  const QString plugin = pluginFileIn(roots.first());
  ASSERT_FALSE(plugin.isEmpty());
  const PluginVerdict verdict = checkPluginFile(plugin);
  EXPECT_TRUE(verdict.ok) << qPrintable(verdict.reason);
  const QStringList usable = usableImportRoots(roots);
  ASSERT_EQ(usable, roots);

  QQmlEngine engine;
  QString error;
  EXPECT_TRUE(greetingFrom(engine, &error).isEmpty());  // not on the import path yet
  QQmlEngine withPath;
  for (const QString& root : usable) withPath.addImportPath(root);
  EXPECT_EQ(greetingFrom(withPath, &error), QStringLiteral("from the plugin")) << qPrintable(error);
}

TEST(PluginImport, AnImportPathFolderLoadsToo) {
  if (!QFile::exists(builtModuleDir())) GTEST_SKIP() << "the test plugin is not built";
  QTemporaryDir extra;
  ASSERT_TRUE(extra.isValid());
  ASSERT_TRUE(copyModule(extra.path()));
  const QStringList usable = usableImportRoots(importRoots(QString(), {extra.path()}));
  ASSERT_EQ(usable.size(), 1);
  QQmlEngine engine;
  engine.addImportPath(usable.first());
  QString error;
  EXPECT_EQ(greetingFrom(engine, &error), QStringLiteral("from the plugin")) << qPrintable(error);
}

TEST(PluginImport, APluginBuiltForAnotherQtMinorIsRefusedBeforeLoading) {
  if (!QFile::exists(builtModuleDir())) GTEST_SKIP() << "the test plugin is not built";
  QTemporaryDir extra;
  ASSERT_TRUE(extra.isValid());
  ASSERT_TRUE(copyModule(extra.path()));
  const QString plugin = pluginFileIn(extra.path());
  ASSERT_FALSE(plugin.isEmpty());
  ASSERT_TRUE(checkPluginFile(plugin).ok);

  // The header follows the 12-byte magic string: format version, Qt major, Qt minor, flags.
  ASSERT_TRUE(patchFile(plugin, [](QByteArray& bytes) {
    const QByteArray magic("QTMETADATA !", 12);
    const qsizetype at = bytes.indexOf(magic);
    if (at < 0) return false;
    const qsizetype minor = at + magic.size() + 2;
    if (static_cast<uchar>(bytes[minor]) != QT_VERSION_MINOR) return false;
    bytes[minor] = static_cast<char>(QT_VERSION_MINOR + 1);
    return true;
  }));
  const PluginVerdict verdict = checkPluginFile(plugin);
  EXPECT_FALSE(verdict.ok);
  EXPECT_FALSE(verdict.reason.isEmpty());
  EXPECT_TRUE(usableImportRoots(importRoots(QString(), {extra.path()})).isEmpty());
}

TEST(PluginImport, APluginForAnotherProcessorIsRefusedBeforeLoading) {
  if (!QFile::exists(builtModuleDir())) GTEST_SKIP() << "the test plugin is not built";
  if (hostMachine() == 0) GTEST_SKIP() << "unknown processor";
  QTemporaryDir extra;
  ASSERT_TRUE(extra.isValid());
  ASSERT_TRUE(copyModule(extra.path()));
  const QString plugin = pluginFileIn(extra.path());
  ASSERT_FALSE(plugin.isEmpty());
  ASSERT_TRUE(patchFile(plugin, [](QByteArray& bytes) {
    const quint16 other = hostMachine() == 0x8664 ? 0xAA64 : 0x8664;
    const qsizetype pe = static_cast<uchar>(bytes[0x3C]) | static_cast<uchar>(bytes[0x3D]) << 8;
    bytes[pe + 4] = static_cast<char>(other & 0xff);
    bytes[pe + 5] = static_cast<char>(other >> 8);
    return true;
  }));
  const PluginVerdict verdict = checkPluginFile(plugin);
  EXPECT_FALSE(verdict.ok);
  EXPECT_TRUE(verdict.reason.contains(QStringLiteral("this waku runs as")))
      << qPrintable(verdict.reason);
  EXPECT_TRUE(usableImportRoots(importRoots(QString(), {extra.path()})).isEmpty());
}

TEST(PluginImport, AFolderWithoutPluginsIsUsable) {
  QTemporaryDir extra;
  ASSERT_TRUE(extra.isValid());
  EXPECT_EQ(usableImportRoots({extra.path()}), QStringList{extra.path()});
}
