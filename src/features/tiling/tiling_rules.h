#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <optional>
#include <vector>

#include "features/tiling/window_placer.h"

namespace waku::tiling {

enum class RuleAction {
  Ignore,  // never managed: the tiler leaves the window alone
  Float,   // managed as a floating window: never placed, but toggleFloat can tile it
};

// One user rule. A rule matches when every field it sets matches: `process` and `className` are
// compared whole and without case, `title` is a regular expression searched in the title.
struct Rule {
  QString process;
  QString className;
  QRegularExpression title;
  RuleAction action = RuleAction::Ignore;
};

struct ParsedRules {
  std::vector<Rule> rules;
  QStringList errors;  // one line per entry that was skipped
};

// The rules of the shell: a list of maps `{process, class, title, action}` (`action` is "ignore" or
// "float", default "ignore"). An entry that sets no matcher, has an unknown action or a title that
// is not a valid regular expression is skipped and reported in `errors`.
ParsedRules parseRules(const QVariantList& entries);

// The action of the first rule that matches, or nothing.
std::optional<RuleAction> matchRules(const std::vector<Rule>& rules, const WindowFacts& facts);

// Window classes that are never managed: splash screens and shell phantoms.
bool isBuiltinIgnoredClass(const QString& className);

enum class Verdict {
  Manage,  // tile it
  Float,   // keep it, but do not place it
  Ignore,  // not a tileable window
  Retry,   // not settled yet (minimized or untitled): look again shortly
};

struct Eligibility {
  Verdict verdict = Verdict::Ignore;
  QString reason;  // short, for the log and the tests
  // An Ignore that holds for the life of the window (a tool window stays one). The others depend on
  // a state that can change (hidden, maximized), so a later Show looks again.
  bool lasting = false;
};

// Whether a window belongs to the tiler. A window is managed unless it is: not visible, cloaked,
// iconic (Retry), untitled (Retry), a window of waku, owned by a visible owner, a tool window,
// without a thick frame or caption, maximized, fullscreen, of a built-in ignored class,
// ignored by a rule (a `float` rule gives Float), or the window of an elevated process while waku
// is not elevated (UIPI forbids moving it). `forceTile` skips the user rules and the float
// verdict: the user asked for this window.
Eligibility classify(const WindowFacts& facts, const std::vector<Rule>& rules, bool selfElevated,
                     bool forceTile = false);

}  // namespace waku::tiling
