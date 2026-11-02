#pragma once

// The `Quickshell.Io` module's types: the quickshell_io feature's, registered again under
// Quickshell's names so that `import Quickshell.Io` shows these names and no others of
// waku.services. No code: each struct only carries the registration (QML_FOREIGN).

#include <QtQml/qqmlregistration.h>

#include <QObject>

#include "features/quickshell_io/qml/data_stream_parsers.h"
#include "features/quickshell_io/qml/file_view.h"
#include "features/quickshell_io/qml/json_adapter.h"
#include "features/quickshell_io/qml/qs_process.h"

namespace waku::quickshell {

struct QsProcess {
  Q_GADGET
  QML_FOREIGN(waku::quickshell_io::QsProcess)
  QML_NAMED_ELEMENT(Process)
};

struct QsDataStreamParser {
  Q_GADGET
  QML_FOREIGN(waku::quickshell_io::DataStreamParser)
  QML_NAMED_ELEMENT(DataStreamParser)
  QML_UNCREATABLE("DataStreamParser is a base type; use StdioCollector or SplitParser")
};

struct QsStdioCollector {
  Q_GADGET
  QML_FOREIGN(waku::quickshell_io::StdioCollector)
  QML_NAMED_ELEMENT(StdioCollector)
};

struct QsSplitParser {
  Q_GADGET
  QML_FOREIGN(waku::quickshell_io::SplitParser)
  QML_NAMED_ELEMENT(SplitParser)
};

struct QsFileView {
  Q_GADGET
  QML_FOREIGN(waku::quickshell_io::FileView)
  QML_NAMED_ELEMENT(FileView)
};

struct QsFileViewAdapter {
  Q_GADGET
  QML_FOREIGN(waku::quickshell_io::FileViewAdapter)
  QML_NAMED_ELEMENT(FileViewAdapter)
  QML_UNCREATABLE("FileViewAdapter is a base type; use JsonAdapter")
};

struct QsJsonObject {
  Q_GADGET
  QML_FOREIGN(waku::quickshell_io::JsonObject)
  QML_NAMED_ELEMENT(JsonObject)
};

struct QsJsonAdapter {
  Q_GADGET
  QML_FOREIGN(waku::quickshell_io::JsonAdapter)
  QML_NAMED_ELEMENT(JsonAdapter)
};

// FileViewError again as a namespace of the module, with the same values: the registrar needs the
// namespace's own metadata, which a foreign namespace in another header does not give it.
namespace QsFileViewError {
Q_NAMESPACE
QML_NAMED_ELEMENT(FileViewError)
enum Enum {
  Success = static_cast<int>(waku::quickshell_io::FileViewError::Success),
  Unknown = static_cast<int>(waku::quickshell_io::FileViewError::Unknown),
  FileNotFound = static_cast<int>(waku::quickshell_io::FileViewError::FileNotFound),
  PermissionDenied = static_cast<int>(waku::quickshell_io::FileViewError::PermissionDenied),
  NotAFile = static_cast<int>(waku::quickshell_io::FileViewError::NotAFile),
};
Q_ENUM_NS(Enum)
}  // namespace QsFileViewError

}  // namespace waku::quickshell
