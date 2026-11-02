#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <functional>
#include <map>
#include <memory>

#include "features/quickshell_io/file_access.h"
#include "features/quickshell_io/qml/file_view.h"
#include "features/quickshell_io/qml/json_adapter.h"

using namespace waku::quickshell_io;

namespace {

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 10000) {
  return QTest::qWaitFor(condition, timeoutMs);
}

void writeFile(const QString& path, const QByteArray& data) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(data);
}

QByteArray readFile(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// In-memory disk with injectable errors.
class FakeDisk : public FileAccess {
 public:
  ReadResult read(const QString& path) override {
    QMutexLocker lock(&mutex);
    if (readError != FileError::Success) return {readError, {}};
    const auto it = files.find(path);
    if (it == files.end()) return {FileError::FileNotFound, {}};
    return {FileError::Success, it->second};
  }
  FileError write(const QString& path, const QByteArray& data, bool atomic) override {
    QMutexLocker lock(&mutex);
    lastAtomic = atomic;
    if (writeError != FileError::Success) return writeError;
    files[path] = data;
    return FileError::Success;
  }
  QMutex mutex;
  std::map<QString, QByteArray> files;
  FileError readError = FileError::Success;
  FileError writeError = FileError::Success;
  bool lastAtomic = false;
};

struct FakeScope {
  FakeDisk disk;
  FakeScope() { setFileAccessForTests(&disk); }
  ~FakeScope() { setFileAccessForTests(nullptr); }
};

std::unique_ptr<FileView> view(const QString& path, bool complete = true) {
  auto v = std::make_unique<FileView>();
  v->setPrintErrors(false);
  v->setPath(path);
  if (complete) v->componentComplete();
  return v;
}

}  // namespace

TEST(QuickshellFileView, LoadsInTheBackgroundAndSignalsLoaded) {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("a.txt"));
  writeFile(path, "hello");
  auto v = view(path);
  QSignalSpy loaded(v.get(), &FileView::loaded);
  EXPECT_FALSE(v->isLoaded());
  ASSERT_TRUE(waitFor([&] { return loaded.count() == 1; }));
  EXPECT_TRUE(v->isLoaded());
  EXPECT_EQ(v->text(), QStringLiteral("hello"));
  EXPECT_EQ(v->data(), QByteArray("hello"));
}

TEST(QuickshellFileView, BlockLoadingReadsOnThePath) {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("a.txt"));
  writeFile(path, "now");
  FileView v;
  v.setBlockLoading(true);
  v.setPath(path);
  v.componentComplete();
  EXPECT_TRUE(v.isLoaded());
  EXPECT_EQ(v.text(), QStringLiteral("now"));
}

TEST(QuickshellFileView, PreloadOffLoadsOnFirstAccess) {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("a.txt"));
  writeFile(path, "lazy");
  FileView v;
  v.setPreload(false);
  v.setBlockLoading(true);
  v.setPath(path);
  v.componentComplete();
  EXPECT_FALSE(v.isLoaded());
  EXPECT_EQ(v.text(), QStringLiteral("lazy"));
  EXPECT_TRUE(v.isLoaded());
}

TEST(QuickshellFileView, MissingFileFailsWithFileNotFound) {
  QTemporaryDir dir;
  auto v = view(dir.filePath(QStringLiteral("missing.txt")));
  QSignalSpy failed(v.get(), &FileView::loadFailed);
  ASSERT_TRUE(waitFor([&] { return failed.count() == 1; }));
  EXPECT_EQ(failed.first().at(0).toInt(), static_cast<int>(FileViewError::FileNotFound));
  EXPECT_FALSE(v->isLoaded());
}

TEST(QuickshellFileView, DirectoryFailsWithNotAFile) {
  QTemporaryDir dir;
  auto v = view(dir.path());
  QSignalSpy failed(v.get(), &FileView::loadFailed);
  ASSERT_TRUE(waitFor([&] { return failed.count() == 1; }));
  EXPECT_EQ(failed.first().at(0).toInt(), static_cast<int>(FileViewError::NotAFile));
}

TEST(QuickshellFileView, SetTextCreatesFoldersAndWrites) {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("deep/er/out.txt"));
  auto v = view(QString());
  v->setPath(path);
  QSignalSpy saved(v.get(), &FileView::saved);
  v->setText(QStringLiteral("written"));
  ASSERT_TRUE(waitFor([&] { return saved.count() == 1; }));
  EXPECT_EQ(readFile(path), QByteArray("written"));
}

TEST(QuickshellFileView, BlockWritesWritesAtOnce) {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("w.txt"));
  FileView v;
  v.setBlockWrites(true);
  v.setPath(path);
  v.componentComplete();
  QSignalSpy saved(&v, &FileView::saved);
  v.setData(QByteArray("bytes"));
  EXPECT_EQ(saved.count(), 1);
  EXPECT_EQ(readFile(path), QByteArray("bytes"));
}

TEST(QuickshellFileView, ConsecutiveWritesEndWithTheLastOne) {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("w.txt"));
  auto v = view(path);
  QSignalSpy saved(v.get(), &FileView::saved);
  for (int i = 0; i < 20; ++i) v->setText(QString::number(i));
  v->waitForJob();
  ASSERT_TRUE(waitFor([&] { return readFile(path) == QByteArray("19"); }));
}

TEST(QuickshellFileView, ReloadPicksUpExternalChanges) {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("a.txt"));
  writeFile(path, "one");
  auto v = view(path);
  QSignalSpy loaded(v.get(), &FileView::loaded);
  ASSERT_TRUE(waitFor([&] { return loaded.count() == 1; }));
  writeFile(path, "two");
  v->reload();
  ASSERT_TRUE(waitFor([&] { return loaded.count() == 2; }));
  EXPECT_EQ(v->text(), QStringLiteral("two"));
}

TEST(QuickshellFileView, PathChangeResetsAndLoadsTheNewFile) {
  QTemporaryDir dir;
  writeFile(dir.filePath(QStringLiteral("a.txt")), "A");
  writeFile(dir.filePath(QStringLiteral("b.txt")), "B");
  auto v = view(dir.filePath(QStringLiteral("a.txt")));
  QSignalSpy loaded(v.get(), &FileView::loaded);
  ASSERT_TRUE(waitFor([&] { return loaded.count() == 1; }));
  v->setPath(dir.filePath(QStringLiteral("b.txt")));
  EXPECT_FALSE(v->isLoaded());
  ASSERT_TRUE(waitFor([&] { return loaded.count() == 2; }));
  EXPECT_EQ(v->text(), QStringLiteral("B"));
}

TEST(QuickshellFileView, WatchChangesReportsExternalEditsButNotOwnWrites) {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("watched.txt"));
  writeFile(path, "v1");
  FileView v;
  v.setPrintErrors(false);
  v.setWatchChanges(true);
  v.setPath(path);
  v.componentComplete();
  QSignalSpy loaded(&v, &FileView::loaded);
  QSignalSpy changed(&v, &FileView::fileChanged);
  ASSERT_TRUE(waitFor([&] { return loaded.count() == 1; }));

  QSignalSpy saved(&v, &FileView::saved);
  v.setText(QStringLiteral("own"));
  ASSERT_TRUE(waitFor([&] { return saved.count() == 1; }));
  QTest::qWait(400);
  EXPECT_EQ(changed.count(), 0);

  writeFile(path, "external");
  ASSERT_TRUE(waitFor([&] { return changed.count() >= 1; }));
  v.reload();
  ASSERT_TRUE(waitFor([&] { return v.text() == QStringLiteral("external"); }));

  // The watch survives the first event: a second edit is reported too.
  const int before = changed.count();
  writeFile(path, "again");
  ASSERT_TRUE(waitFor([&] { return changed.count() > before; }));
}

TEST(QuickshellFileView, ReadErrorsComeFromTheFileAccess) {
  FakeScope scope;
  scope.disk.readError = FileError::PermissionDenied;
  auto v = view(QStringLiteral("C:/fake/denied.txt"));
  QSignalSpy failed(v.get(), &FileView::loadFailed);
  ASSERT_TRUE(waitFor([&] { return failed.count() == 1; }));
  EXPECT_EQ(failed.first().at(0).toInt(), static_cast<int>(FileViewError::PermissionDenied));
}

TEST(QuickshellFileView, WriteErrorsComeFromTheFileAccessAndAtomicIsPassed) {
  FakeScope scope;
  auto v = view(QStringLiteral("C:/fake/out.txt"));
  QSignalSpy saved(v.get(), &FileView::saved);
  QSignalSpy failed(v.get(), &FileView::saveFailed);
  v->setText(QStringLiteral("ok"));
  ASSERT_TRUE(waitFor([&] { return saved.count() == 1; }));
  EXPECT_TRUE(scope.disk.lastAtomic);
  EXPECT_EQ(scope.disk.files[QStringLiteral("C:/fake/out.txt")], QByteArray("ok"));

  v->setAtomicWrites(false);
  scope.disk.writeError = FileError::PermissionDenied;
  v->setText(QStringLiteral("no"));
  ASSERT_TRUE(waitFor([&] { return failed.count() == 1; }));
  EXPECT_EQ(failed.first().at(0).toInt(), static_cast<int>(FileViewError::PermissionDenied));
  EXPECT_FALSE(scope.disk.lastAtomic);
}

TEST(QuickshellFileView, DestroyingWhileAJobRunsIsSafe) {
  FakeScope scope;
  scope.disk.files[QStringLiteral("C:/fake/a.txt")] = "x";
  bool signalled = false;
  {
    auto v = view(QStringLiteral("C:/fake/a.txt"));
    for (auto signal : {&FileView::loaded, &FileView::saved}) {
      QObject::connect(v.get(), signal, [&] { signalled = true; });
    }
    QObject::connect(v.get(), &FileView::saveFailed, [&] { signalled = true; });
    v->setText(QStringLiteral("y"));
  }
  // The job runs on a pool thread and ends after the view is gone: the write lands, nothing is
  // delivered to the dead object.
  QThreadPool::globalInstance()->waitForDone(5000);
  QCoreApplication::sendPostedEvents();  // the job's queued result, for nobody
  {
    QMutexLocker lock(&scope.disk.mutex);
    EXPECT_EQ(scope.disk.files[QStringLiteral("C:/fake/a.txt")], QByteArray("y"));
  }
  EXPECT_FALSE(signalled);
}

TEST(QuickshellFileView, WaitForJobAppliesTheResult) {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("a.txt"));
  writeFile(path, "sync");
  auto v = view(path);
  EXPECT_TRUE(v->waitForJob());
  EXPECT_TRUE(v->isLoaded());
  EXPECT_EQ(v->text(), QStringLiteral("sync"));
  EXPECT_FALSE(v->waitForJob());
}

// ---- JsonAdapter, from QML ----

namespace {

struct QmlFile {
  QQmlEngine engine;
  std::unique_ptr<QObject> root;
  QString error;
  QmlFile(const QString& path, const QString& extra = QString()) {
    QQmlComponent component(&engine);
    component.setData(QStringLiteral(R"(
import QtQml
import waku.services
QtObject {
    id: root
    property var view: v
    property var adapterObject: a
    property int updates: 0
    property var errors: []
    property var holder: FileView {
        id: v
        path: "%1"
        printErrors: false
        onAdapterUpdated: { root.updates++; writeAdapter() }
        onLoadFailed: err => root.errors.push(err)
        %2
        JsonAdapter {
            id: a
            property string name: "default"
            property int count: 1
            property bool flag: false
            property real ratio: 0.5
            property list<string> tags: ["x"]
            property var extra: ({})
            property JsonObject sub: JsonObject { property int level: 1; property string label: "l" }
        }
    }
}
)")
                          .arg(path, extra)
                          .toUtf8(),
                      QUrl());
    root.reset(component.create());
    error = component.errorString();
  }
};

}  // namespace

TEST(QuickshellJsonAdapter, LoadsKeysIntoQmlProperties) {
  QTemporaryDir dir;
  const QString path = QDir::fromNativeSeparators(dir.filePath(QStringLiteral("a.json")));
  writeFile(path, R"({"name":"bob","count":7,"flag":true,"ratio":2.5,"tags":["a","b"],)"
                  R"("extra":{"k":1},"sub":{"level":9}})");
  QmlFile f(path);
  ASSERT_TRUE(f.root) << f.error.toStdString();
  auto* view = f.root->property("view").value<FileView*>();
  ASSERT_TRUE(view);
  ASSERT_TRUE(waitFor([&] { return view->isLoaded(); }));
  QObject* a = f.root->property("adapterObject").value<QObject*>();
  ASSERT_TRUE(a);
  EXPECT_EQ(a->property("name").toString(), QStringLiteral("bob"));
  EXPECT_EQ(a->property("count").toInt(), 7);
  EXPECT_TRUE(a->property("flag").toBool());
  EXPECT_DOUBLE_EQ(a->property("ratio").toDouble(), 2.5);
  EXPECT_EQ(a->property("tags").toStringList(),
            (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
  EXPECT_EQ(a->property("extra").toMap().value(QStringLiteral("k")).toInt(), 1);
  QObject* sub = a->property("sub").value<QObject*>();
  ASSERT_TRUE(sub);
  EXPECT_EQ(sub->property("level").toInt(), 9);
  EXPECT_EQ(sub->property("label").toString(), QStringLiteral("l"));  // absent key keeps its value
  EXPECT_EQ(f.root->property("updates").toInt(), 0);                  // loading is not an update
}

TEST(QuickshellJsonAdapter, QmlChangesEmitAdapterUpdatedAndWriteTheFile) {
  QTemporaryDir dir;
  const QString path = QDir::fromNativeSeparators(dir.filePath(QStringLiteral("a.json")));
  writeFile(path, R"({"name":"bob"})");
  QmlFile f(path);
  ASSERT_TRUE(f.root) << f.error.toStdString();
  auto* view = f.root->property("view").value<FileView*>();
  ASSERT_TRUE(waitFor([&] { return view->isLoaded(); }));
  QObject* a = f.root->property("adapterObject").value<QObject*>();
  a->setProperty("name", QStringLiteral("alice"));
  QObject* sub = a->property("sub").value<QObject*>();
  sub->setProperty("level", 4);
  EXPECT_GE(f.root->property("updates").toInt(), 2);
  view->waitForJob();
  ASSERT_TRUE(waitFor([&] {
    const auto doc = QJsonDocument::fromJson(readFile(path));
    return doc.object().value(QStringLiteral("name")).toString() == QStringLiteral("alice") &&
           doc.object()
                   .value(QStringLiteral("sub"))
                   .toObject()
                   .value(QStringLiteral("level"))
                   .toInt() == 4;
  }));
  const QJsonObject written = QJsonDocument::fromJson(readFile(path)).object();
  EXPECT_EQ(written.value(QStringLiteral("count")).toInt(), 1);
  EXPECT_EQ(written.value(QStringLiteral("tags")).toArray().size(), 1);
}

TEST(QuickshellJsonAdapter, MissingFileReportsFileViewErrorAndCanBeCreated) {
  QTemporaryDir dir;
  const QString path = QDir::fromNativeSeparators(dir.filePath(QStringLiteral("new/a.json")));
  QmlFile f(path);
  ASSERT_TRUE(f.root) << f.error.toStdString();
  ASSERT_TRUE(waitFor([&] {
    return f.root->property("errors")
               .value<QJSValue>()
               .property(QStringLiteral("length"))
               .toInt() == 1;
  }));
  const int err = f.root->property("errors").value<QJSValue>().property(0).toInt();
  EXPECT_EQ(err, static_cast<int>(FileViewError::FileNotFound));
  auto* view = f.root->property("view").value<FileView*>();
  view->writeAdapter();
  ASSERT_TRUE(waitFor([&] { return QFile::exists(path); }));
  EXPECT_EQ(
      QJsonDocument::fromJson(readFile(path)).object().value(QStringLiteral("name")).toString(),
      QStringLiteral("default"));
}

TEST(QuickshellJsonAdapter, FileViewErrorEnumIsAvailableInQml) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQml
import waku.services
QtObject {
    property int notFound: FileViewError.FileNotFound
    property int denied: FileViewError.PermissionDenied
    property int success: FileViewError.Success
}
)",
                    QUrl());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_TRUE(root) << component.errorString().toStdString();
  EXPECT_EQ(root->property("notFound").toInt(), static_cast<int>(FileViewError::FileNotFound));
  EXPECT_EQ(root->property("denied").toInt(), static_cast<int>(FileViewError::PermissionDenied));
  EXPECT_EQ(root->property("success").toInt(), 0);
}
