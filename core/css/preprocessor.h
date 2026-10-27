#pragma once

#include <QList>
#include <QSet>
#include <QString>

namespace yasb::css {

struct LogRecord {
  QString level;  // "WARNING" or "ERROR", as in the Python oracle
  QString message;
};

// Port of core/utils/css_processor.py: @import, :root variables, #RRGGBBAA, -qproperty- and url().
class CssPreprocessor {
 public:
  explicit CssPreprocessor(const QString& cssPath);

  QString process();
  const QList<LogRecord>& log() const { return log_; }

 private:
  QString readCssFile(const QString& path);
  QString processImports(const QString& css);
  QString resolveUrls(const QString& css);
  void warn(const QString& message);
  void error(const QString& message);

  QString cssPath_;
  QString basePath_;
  QString cssContent_;
  QSet<QString> importedFiles_;
  QList<LogRecord> log_;
};

}  // namespace yasb::css
