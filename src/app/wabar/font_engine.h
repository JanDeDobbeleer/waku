#pragma once

#include <QString>
#include <QStringList>

// `--font-engine=gdi|directwrite` of wabar.exe. The font engine is chosen by the platform plugin
// string, which Qt reads when the QGuiApplication is built, before the command line parser runs,
// so the option is read from the raw arguments here.
//
// gdi (default): the lean choice. Qt's GDI font database loads fonts on demand and keeps the
//   working set small. It ignores variable-font axes (`font.variableAxes`), so a variable font
//   renders at its default instance.
// directwrite: Qt's default engine. It honours variable-font axes and costs about 2 MB more
//   working set (it maps every installed font at start-up). A shell that needs axes opts in.
namespace wabar::app {

enum class FontEngine { Gdi, DirectWrite };

struct FontEngineChoice {
  FontEngine engine = FontEngine::Gdi;
  // Empty when the argument was absent or valid; otherwise what was wrong. The engine then stays
  // the default.
  QString error;
};

// Reads `--font-engine=<name>` or `--font-engine <name>` from `arguments` (argv without the
// program name is fine; the first element is not special). The last occurrence wins. Names are
// case-insensitive.
inline FontEngineChoice parseFontEngine(const QStringList& arguments) {
  FontEngineChoice choice;
  const QString flag = QStringLiteral("--font-engine");
  for (qsizetype i = 0; i < arguments.size(); ++i) {
    const QString& arg = arguments.at(i);
    QString value;
    if (arg == flag) {
      if (i + 1 >= arguments.size()) {
        choice = FontEngineChoice{FontEngine::Gdi, QStringLiteral("--font-engine needs a value")};
        continue;
      }
      value = arguments.at(++i);
    } else if (arg.startsWith(flag + QLatin1Char('='))) {
      value = arg.mid(flag.size() + 1);
    } else {
      continue;
    }
    if (value.compare(QStringLiteral("gdi"), Qt::CaseInsensitive) == 0) {
      choice = FontEngineChoice{FontEngine::Gdi, {}};
    } else if (value.compare(QStringLiteral("directwrite"), Qt::CaseInsensitive) == 0) {
      choice = FontEngineChoice{FontEngine::DirectWrite, {}};
    } else {
      choice =
          FontEngineChoice{FontEngine::Gdi, QStringLiteral("unknown font engine \"%1\" (gdi or "
                                                           "directwrite)")
                                                .arg(value)};
    }
  }
  return choice;
}

// The QT_QPA_PLATFORM value for `engine`.
inline QString platformSpec(FontEngine engine) {
  return engine == FontEngine::DirectWrite ? QStringLiteral("windows")
                                           : QStringLiteral("windows:fontengine=gdi");
}

}  // namespace wabar::app
