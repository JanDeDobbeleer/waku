// Transition engine subset for the YASB port. Behaviour (cascade ordering, easing names,
// :clicked flow, cursor map) follows qt_css_engine (MIT, (c) 2026 George Sladkovsky),
// re-implemented as a simplified app-wide event filter.
#include "core/css/transition_engine.h"

#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QMouseEvent>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <memory>

namespace yasb::css {

Q_LOGGING_CATEGORY(lcTransitions, "yasb.css.transitions")

using namespace Qt::StringLiterals;

namespace {

constexpr const char* kScopeProp = "_anim_scope";
constexpr const char* kEffectProp = "_yasb_opacity_effect";

struct Val {
  enum Kind { Num, Color, Opacity } kind = Num;
  double n = 0;
  QColor c;
  bool operator==(const Val& o) const { return kind == o.kind && n == o.n && c == o.c; }
};

bool isColorProp(const QString& p) { return p == u"color"_s || p.endsWith(u"-color"_s); }

bool isNumericProp(const QString& p) {
  static const QSet<QString> exact{u"width"_s,     u"height"_s,     u"min-width"_s,
                                   u"max-width"_s, u"min-height"_s, u"max-height"_s};
  return exact.contains(p) || p.startsWith(u"padding"_s) || p.startsWith(u"margin"_s) ||
         (p.startsWith(u"border"_s) && p.endsWith(u"-width"_s));
}

bool isAnimatable(const QString& p) {
  return p == u"opacity"_s || isColorProp(p) || isNumericProp(p);
}

std::optional<QColor> parseColor(const QString& text) {
  static const QRegularExpression fn(
      u"^rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*(?:,\\s*([\\d.]+%?)\\s*)?\\)$"_s,
      QRegularExpression::CaseInsensitiveOption);
  const QString s = text.trimmed();
  const auto m = fn.match(s);
  if (m.hasMatch()) {
    double a = 255;
    if (!m.captured(4).isEmpty()) {
      QString as = m.captured(4);
      const bool pct = as.endsWith(u'%');
      if (pct) as.chop(1);
      const double v = as.toDouble();
      a = pct ? v * 2.55 : (v <= 1.0 ? v * 255.0 : v);
    }
    return QColor(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt(),
                  std::clamp(static_cast<int>(std::lround(a)), 0, 255));
  }
  QColor c(s);
  if (c.isValid()) return c;
  return std::nullopt;
}

std::optional<Val> parseVal(const QString& prop, const QString& text) {
  Val v;
  if (prop == u"opacity"_s) {
    bool ok = false;
    const double d = text.trimmed().toDouble(&ok);
    if (!ok) return std::nullopt;
    v.kind = Val::Opacity;
    v.n = std::clamp(d, 0.0, 1.0);
    return v;
  }
  if (isColorProp(prop)) {
    const auto c = parseColor(text);
    if (!c) return std::nullopt;
    v.kind = Val::Color;
    v.c = *c;
    return v;
  }
  static const QRegularExpression num(u"^(-?(?:\\d+\\.?\\d*|\\.\\d+))(?:px)?$"_s);
  const auto m = num.match(text.trimmed());
  if (!m.hasMatch()) return std::nullopt;
  v.n = m.captured(1).toDouble();
  return v;
}

QString format(const Val& v) {
  switch (v.kind) {
    case Val::Color:
      return u"rgba(%1, %2, %3, %4)"_s.arg(v.c.red())
          .arg(v.c.green())
          .arg(v.c.blue())
          .arg(v.c.alpha());
    case Val::Opacity:
      return QString::number(v.n, 'f', 3);
    case Val::Num:
      break;
  }
  return QString::number(v.n, 'f', 3) + u"px"_s;
}

Val lerp(const Val& a, const Val& b, double t) {
  Val r = b;
  if (b.kind == Val::Color) {
    auto mix = [t](int x, int y) {
      return std::clamp(static_cast<int>(std::lround(x + (y - x) * t)), 0, 255);
    };
    r.c = QColor(mix(a.c.red(), b.c.red()), mix(a.c.green(), b.c.green()),
                 mix(a.c.blue(), b.c.blue()), mix(a.c.alpha(), b.c.alpha()));
  } else {
    r.n = a.n + (b.n - a.n) * t;
  }
  return r;
}

struct Ease {
  QEasingCurve curve{QEasingCurve::InOutQuad};
  int steps = 0;
  bool stepStart = false;

  double at(double p) const {
    if (steps > 0) {
      const double s = stepStart ? std::ceil(p * steps) : std::floor(p * steps);
      return std::clamp(s / steps, 0.0, 1.0);
    }
    return curve.valueForProgress(p);
  }
};

Ease parseEase(const QString& name) {
  Ease e;
  const QString n = name.trimmed().toLower();
  static const QRegularExpression bez(u"^cubic-bezier\\(([^)]*)\\)$"_s);
  static const QRegularExpression stp(u"^steps\\(\\s*(\\d+)\\s*(?:,\\s*([a-z-]+))?\\s*\\)$"_s);
  if (n == u"linear"_s) {
    e.curve = QEasingCurve(QEasingCurve::Linear);
  } else if (n == u"ease-in"_s) {
    e.curve = QEasingCurve(QEasingCurve::InCubic);
  } else if (n == u"ease-out"_s) {
    e.curve = QEasingCurve(QEasingCurve::OutCubic);
  } else if (n == u"ease-in-out"_s) {
    e.curve = QEasingCurve(QEasingCurve::InOutCubic);
  } else if (const auto m = bez.match(n); m.hasMatch()) {
    const auto parts = m.captured(1).split(u',');
    if (parts.size() == 4) {
      QEasingCurve c(QEasingCurve::BezierSpline);
      c.addCubicBezierSegment({parts[0].toDouble(), parts[1].toDouble()},
                              {parts[2].toDouble(), parts[3].toDouble()}, {1.0, 1.0});
      e.curve = c;
    }
  } else if (const auto s = stp.match(n); s.hasMatch()) {
    e.steps = std::max(1, s.captured(1).toInt());
    e.stepStart = s.captured(2) == u"start"_s || s.captured(2) == u"jump-start"_s;
  }
  return e;
}

std::optional<Qt::CursorShape> cursorShape(const QString& name) {
  static const QHash<QString, Qt::CursorShape> map{{u"default"_s, Qt::ArrowCursor},
                                                   {u"pointer"_s, Qt::PointingHandCursor},
                                                   {u"text"_s, Qt::IBeamCursor},
                                                   {u"wait"_s, Qt::WaitCursor},
                                                   {u"progress"_s, Qt::BusyCursor},
                                                   {u"crosshair"_s, Qt::CrossCursor},
                                                   {u"move"_s, Qt::SizeAllCursor},
                                                   {u"not-allowed"_s, Qt::ForbiddenCursor},
                                                   {u"help"_s, Qt::WhatsThisCursor},
                                                   {u"grab"_s, Qt::OpenHandCursor},
                                                   {u"grabbing"_s, Qt::ClosedHandCursor},
                                                   {u"ew-resize"_s, Qt::SizeHorCursor},
                                                   {u"ns-resize"_s, Qt::SizeVerCursor},
                                                   {u"nwse-resize"_s, Qt::SizeFDiagCursor},
                                                   {u"nesw-resize"_s, Qt::SizeBDiagCursor},
                                                   {u"col-resize"_s, Qt::SplitHCursor},
                                                   {u"row-resize"_s, Qt::SplitVCursor},
                                                   {u"none"_s, Qt::BlankCursor}};
  const auto it = map.find(name.trimmed().toLower());
  if (it == map.end()) return std::nullopt;
  return *it;
}

int pseudoPriority(const QStringList& set) {
  int p = 0;
  for (const auto& s : set) {
    if (s == u":clicked"_s || s == u"clicked"_s)
      p = std::max(p, 3);
    else if (s == u":pressed"_s || s == u"pressed"_s)
      p = std::max(p, 2);
    else
      p = std::max(p, 1);
  }
  return p;
}

QString bare(const QString& pseudo) { return pseudo.startsWith(u':') ? pseudo.mid(1) : pseudo; }

}  // namespace

struct TransitionEngine::Impl {
  struct Anim {
    QVariantAnimation* a = nullptr;
    Val from, to;
    Ease ease;
    int delay = 0;
    int duration = 0;
  };

  struct Ctx {
    QPointer<QWidget> w;
    QSet<QString> active;
    QHash<QString, Val> cur;
    QHash<QString, QString> written;
    QHash<QString, Anim> anims;
    QString origStyle;
    QString cursorName;
    bool styleCaptured = false;
    bool writing = false;
  };

  TransitionEngine* q = nullptr;
  ParsedStyleSheet parsed;
  QList<QList<CompiledSegment>> compiled;
  QHash<QWidget*, std::shared_ptr<Ctx>> ctxs;
  QSet<QString> warned;
  int nextScope = 1;

  void warnOnce(const StyleRule& r, const QString& construct) {
    const QString key = r.selector + u"|"_s + construct;
    if (warned.contains(key)) return;
    warned.insert(key);
    qCWarning(lcTransitions).noquote()
        << u"unsupported %1 in '%2' (line %3)"_s.arg(construct, r.selector).arg(r.line);
  }

  void audit() {
    static const QRegularExpression combinator(u"[>+~]|:not\\(|:nth-|:is\\(|:where\\("_s);
    for (const auto& r : std::as_const(parsed.rules)) {
      if (r.has(u"box-shadow"_s)) warnOnce(r, u"box-shadow"_s);
      if (r.droppedSubcontrolTransition) warnOnce(r, u"transition on sub-control"_s);
      for (const auto& t : r.transitions) {
        if (t.prop != u"all"_s && !isAnimatable(t.prop))
          warnOnce(r, u"transition of '%1'"_s.arg(t.prop));
      }
      if (!r.subcontrol) {
        if (combinator.match(r.selector).hasMatch()) warnOnce(r, u"selector combinator"_s);
        for (const auto& p : r.pseudoSet) {
          const QString b = bare(p);
          if (b == u"checked"_s || b == u"active"_s) warnOnce(r, u"pseudo-class :%1"_s.arg(b));
        }
      }
    }
  }

  // ---- matching ----
  static bool matchSegment(const QWidget* w, const CompiledSegment& s) {
    if (!s.tag.isEmpty() && s.tag != u"*"_s) {
      bool ok = false;
      for (const QMetaObject* m = w->metaObject(); m && !ok; m = m->superClass()) {
        ok = s.tag == QLatin1String(m->className());
      }
      if (!ok) return false;
    }
    if (!s.objName.isEmpty() && w->objectName() != s.objName) return false;
    if (!s.classes.isEmpty()) {
      const QStringList have =
          w->property("class").toString().split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts);
      for (const auto& c : s.classes) {
        if (!have.contains(c)) return false;
      }
    }
    for (const auto& a : s.attrs) {
      const QVariant v = w->property(a.name.toUtf8().constData());
      if (a.op == u"exists"_s) {
        if (!v.isValid()) return false;
      } else if (a.op == u"="_s) {
        if (!v.isValid() || !a.value || v.toString() != *a.value) return false;
      } else {
        return false;
      }
    }
    return true;
  }

  bool matches(const QWidget* w, qsizetype ruleIdx) const {
    const auto& segs = compiled[ruleIdx];
    if (segs.isEmpty() || !matchSegment(w, segs.last())) return false;
    const QWidget* cur = w;
    for (qsizetype i = segs.size() - 2; i >= 0; --i) {
      cur = cur->parentWidget();
      while (cur && !matchSegment(cur, segs[i])) cur = cur->parentWidget();
      if (!cur) return false;
    }
    return true;
  }

  // Rules matching `w` whose pseudo-classes are all active, in cascade order.
  QList<const StyleRule*> cascade(const QWidget* w, const QSet<QString>& active) const {
    QList<const StyleRule*> out;
    for (qsizetype i = 0; i < parsed.rules.size(); ++i) {
      const auto& r = parsed.rules[i];
      if (r.subcontrol) continue;
      bool ok = true;
      for (const auto& p : r.pseudoSet) {
        if (!active.contains(bare(p))) {
          ok = false;
          break;
        }
      }
      if (ok && matches(w, i)) out.push_back(&r);
    }
    std::stable_sort(out.begin(), out.end(), [](const StyleRule* a, const StyleRule* b) {
      return std::tuple(a->scopeRank, a->specificity, pseudoPriority(a->pseudoSet), a->order) <
             std::tuple(b->scopeRank, b->specificity, pseudoPriority(b->pseudoSet), b->order);
    });
    return out;
  }

  static QHash<QString, QString> fold(const QList<const StyleRule*>& rules) {
    QHash<QString, QString> props;
    for (const auto* r : rules) {
      for (const auto& d : r->properties) props[d.first] = d.second;
    }
    return props;
  }

  static QHash<QString, TransitionSpec> specs(const QList<const StyleRule*>& rules) {
    QHash<QString, TransitionSpec> out;
    for (const auto* r : rules) {
      if (r->resetsTransitions) out.clear();
      for (const auto& t : r->transitions) out[t.prop] = t;
    }
    return out;
  }

  // ---- context ----
  std::shared_ptr<Ctx> ctxFor(QWidget* w, bool create) {
    if (const auto it = ctxs.find(w); it != ctxs.end()) return *it;
    if (!create) return nullptr;
    bool relevant = false;
    for (qsizetype i = 0; i < parsed.rules.size() && !relevant; ++i) {
      const auto& r = parsed.rules[i];
      if (r.subcontrol) continue;
      if ((!r.pseudoSet.isEmpty() || !r.transitions.isEmpty() || r.hasCursorProp ||
           r.has(u"opacity"_s)) &&
          matches(w, i)) {
        relevant = true;
      }
    }
    if (!relevant) return nullptr;
    auto c = std::make_shared<Ctx>();
    c->w = w;
    ctxs.insert(w, c);
    QObject::connect(w, &QObject::destroyed, q, [this, w] { ctxs.remove(w); });
    return c;
  }

  void flush(Ctx& c) {
    QWidget* w = c.w;
    if (!w) return;
    if (!c.styleCaptured) {
      c.origStyle = w->styleSheet();
      c.styleCaptured = true;
    }
    c.writing = true;
    if (c.written.isEmpty()) {
      w->setStyleSheet(c.origStyle);
    } else {
      if (!w->property(kScopeProp).isValid()) w->setProperty(kScopeProp, nextScope++);
      QStringList keys = c.written.keys();
      keys.sort();
      QString body;
      for (const auto& k : keys) body += u"%1: %2; "_s.arg(k, c.written[k]);
      w->setStyleSheet(c.origStyle +
                       u"\n*[%1=\"%2\"] { %3}"_s.arg(QString::fromLatin1(kScopeProp),
                                                     w->property(kScopeProp).toString(), body));
    }
    c.writing = false;
  }

  void applyOpacity(Ctx& c, double v, bool animating) {
    QWidget* w = c.w;
    if (!w) return;
    auto* eff = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    const bool ours = eff && eff->property(kEffectProp).toBool();
    if (w->graphicsEffect() && !ours) return;  // never replace a foreign effect
    if (v >= 0.999 && !animating) {
      if (ours) w->setGraphicsEffect(nullptr);
      return;
    }
    if (!eff) {
      eff = new QGraphicsOpacityEffect(w);
      eff->setProperty(kEffectProp, true);
      w->setGraphicsEffect(eff);
    }
    eff->setOpacity(v);
  }

  void apply(Ctx& c, const QString& prop, const Val& v, bool animating) {
    if (v.kind == Val::Opacity) {
      applyOpacity(c, v.n, animating);
    } else {
      c.written[prop] = format(v);
      flush(c);
    }
  }

  static std::optional<Val> defaultVal(const QWidget* w, const QString& prop) {
    Val v;
    if (prop == u"opacity"_s) {
      v.kind = Val::Opacity;
      v.n = 1.0;
      return v;
    }
    if (prop == u"width"_s) {
      v.n = w->width();
      return v;
    }
    if (prop == u"height"_s) {
      v.n = w->height();
      return v;
    }
    if (prop.startsWith(u"padding"_s) || prop.startsWith(u"margin"_s) ||
        prop.endsWith(u"-width"_s)) {
      v.n = 0;
      return v;
    }
    return std::nullopt;
  }

  void stopAnim(Ctx& c, const QString& prop) {
    const auto it = c.anims.find(prop);
    if (it == c.anims.end()) return;
    QVariantAnimation* a = it->a;
    c.anims.erase(it);
    a->stop();
    a->deleteLater();
  }

  void startAnim(const std::shared_ptr<Ctx>& cp, const QString& prop, const Val& from,
                 const Val& to, const TransitionSpec& spec) {
    Ctx& c = *cp;
    stopAnim(c, prop);
    Anim an;
    an.from = from;
    an.to = to;
    an.ease = parseEase(spec.easing);
    an.delay = spec.delayMs;
    an.duration = std::max(1, spec.durationMs);
    const int total = an.delay + an.duration;
    an.a = new QVariantAnimation(q);
    an.a->setDuration(total);
    an.a->setStartValue(0.0);
    an.a->setEndValue(1.0);
    c.anims.insert(prop, an);
    const std::weak_ptr<Ctx> weak = cp;
    QObject::connect(an.a, &QVariantAnimation::valueChanged, q,
                     [this, weak, prop](const QVariant& value) {
                       const auto cc = weak.lock();
                       if (!cc || !cc->w) return;
                       const auto it = cc->anims.find(prop);
                       if (it == cc->anims.end()) return;
                       const double elapsed = value.toDouble() * (it->delay + it->duration);
                       const double p = std::clamp((elapsed - it->delay) / it->duration, 0.0, 1.0);
                       const Val v = lerp(it->from, it->to, it->ease.at(p));
                       cc->cur[prop] = v;
                       apply(*cc, prop, v, true);
                     });
    QObject::connect(an.a, &QVariantAnimation::finished, q, [this, weak, prop] {
      const auto cc = weak.lock();
      if (!cc) return;
      const auto it = cc->anims.find(prop);
      if (it == cc->anims.end()) return;
      QVariantAnimation* a = it->a;
      const Val to = it->to;
      cc->anims.erase(it);
      a->deleteLater();
      cc->cur[prop] = to;
      if (cc->w) apply(*cc, prop, to, false);
    });
    an.a->start();
  }

  // Longest delay+duration among running animations, in ms.
  static int pendingMs(const Ctx& c) {
    int m = 0;
    for (const auto& a : c.anims) m = std::max(m, a.delay + a.duration);
    return m;
  }

  void evaluate(const std::shared_ptr<Ctx>& cp) {
    Ctx& c = *cp;
    QWidget* w = c.w;
    if (!w) return;
    const auto activeRules = cascade(w, c.active);
    const auto baseRules = cascade(w, {});
    const auto target = fold(activeRules);
    const auto base = fold(baseRules);
    const auto sp = specs(activeRules);

    QSet<QString> props;
    for (const auto* r : activeRules) {
      for (const auto& d : r->properties) {
        if (isAnimatable(d.first)) props.insert(d.first);
      }
    }
    for (const auto& k : c.cur.keys()) props.insert(k);

    for (const auto& prop : std::as_const(props)) {
      const bool varies = target.value(prop) != base.value(prop) || c.cur.contains(prop);
      if (!varies && !c.anims.contains(prop)) {
        if (prop == u"opacity"_s && target.contains(prop) && !c.cur.contains(prop)) {
          // static opacity still needs the effect
        } else {
          continue;
        }
      }
      QString raw = target.value(prop, base.value(prop));
      std::optional<Val> to = raw.isEmpty() ? defaultVal(w, prop) : parseVal(prop, raw);
      if (!to) {
        stopAnim(c, prop);
        c.cur.remove(prop);
        if (c.written.remove(prop)) flush(c);
        continue;
      }
      if (const auto it = c.anims.find(prop); it != c.anims.end() && it->to == *to) continue;

      std::optional<Val> from;
      if (const auto it = c.cur.find(prop); it != c.cur.end()) {
        from = *it;
      } else if (base.contains(prop)) {
        from = parseVal(prop, base.value(prop));
      } else {
        from = defaultVal(w, prop);
      }
      if (from && *from == *to && !c.anims.contains(prop)) {
        c.cur[prop] = *to;
        continue;
      }

      auto spec = sp.find(prop);
      if (spec == sp.end()) spec = sp.find(u"all"_s);
      const bool animate =
          from && spec != sp.end() && spec->durationMs + spec->delayMs > 0 && w->isVisible();
      if (animate) {
        c.cur[prop] = *from;
        startAnim(cp, prop, *from, *to, *spec);
      } else {
        stopAnim(c, prop);
        c.cur[prop] = *to;
        apply(c, prop, *to, false);
      }
    }

    applyCursor(c, target);
  }

  void applyCursor(Ctx& c, const QHash<QString, QString>& target) {
    QWidget* w = c.w;
    const QString name = target.value(u"cursor"_s);
    if (name == c.cursorName) return;
    c.cursorName = name;
    const auto shape = name.isEmpty() ? std::nullopt : cursorShape(name);
    if (shape) {
      w->setCursor(*shape);
    } else {
      w->unsetCursor();
    }
  }

  void setPseudo(QWidget* w, const QString& name, bool on) {
    const auto c = ctxFor(w, true);
    if (!c) return;
    if (on == c->active.contains(name)) return;
    if (on) {
      c->active.insert(name);
    } else {
      c->active.remove(name);
    }
    evaluate(c);
  }

  void onPress(QWidget* w) {
    const auto c = ctxFor(w, true);
    if (!c) return;
    c->active.insert(u"pressed"_s);
    bool hasClicked = false;
    for (qsizetype i = 0; i < parsed.rules.size() && !hasClicked; ++i) {
      const auto& r = parsed.rules[i];
      hasClicked = !r.subcontrol && r.pseudoSet.contains(u":clicked"_s) && matches(w, i);
    }
    if (hasClicked) c->active.insert(u"clicked"_s);
    evaluate(c);
    if (hasClicked) {
      // :clicked is a pulse: hold it until the forward animation finishes, then animate back.
      const std::weak_ptr<Ctx> weak = c;
      QTimer::singleShot(pendingMs(*c) + 1, q, [this, weak] {
        const auto cc = weak.lock();
        if (!cc || !cc->w) return;
        cc->active.remove(u"clicked"_s);
        evaluate(cc);
      });
    }
  }
};

TransitionEngine::TransitionEngine(QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()) {
  d_->q = this;
}

TransitionEngine::~TransitionEngine() = default;

QString TransitionEngine::loadStyleSheet(const QString& processedCss) {
  for (auto& c : d_->ctxs) {
    for (auto& a : c->anims) {
      a.a->stop();
      a.a->deleteLater();
    }
    if (c->w) {
      if (!c->written.isEmpty()) {
        c->written.clear();
        d_->flush(*c);
      }
      c->w->unsetCursor();
    }
  }
  d_->ctxs.clear();
  d_->warned.clear();
  d_->parsed = extractRules(processedCss);
  d_->compiled.clear();
  for (const auto& r : std::as_const(d_->parsed.rules)) {
    QList<CompiledSegment> segs;
    for (const auto& s : r.segments) segs.push_back(compileSegment(s));
    d_->compiled.push_back(segs);
  }
  d_->audit();
  return d_->parsed.qss;
}

const QList<StyleRule>& TransitionEngine::rules() const { return d_->parsed.rules; }

void TransitionEngine::install(QCoreApplication* app) { app->installEventFilter(this); }

QString TransitionEngine::currentValue(const QWidget* widget, const QString& prop) const {
  const auto it = d_->ctxs.find(const_cast<QWidget*>(widget));
  if (it == d_->ctxs.end()) return {};
  if (prop == u"opacity"_s) {
    const auto v = (*it)->cur.value(prop);
    return (*it)->cur.contains(prop) ? format(v) : QString();
  }
  return (*it)->written.value(prop);
}

bool TransitionEngine::eventFilter(QObject* watched, QEvent* event) {
  auto* w = qobject_cast<QWidget*>(watched);
  if (!w || d_->parsed.rules.isEmpty()) return false;
  switch (event->type()) {
    case QEvent::Polish: {
      const auto c = d_->ctxFor(w, true);
      if (c) {
        for (const auto& r : std::as_const(d_->parsed.rules)) {
          if (r.pseudoSet.contains(u":hover"_s)) {
            w->setAttribute(Qt::WA_Hover, true);
            break;
          }
        }
        d_->evaluate(c);
      }
      break;
    }
    case QEvent::HoverEnter:
    case QEvent::Enter:
      d_->setPseudo(w, u"hover"_s, true);
      break;
    case QEvent::HoverLeave:
    case QEvent::Leave:
      d_->setPseudo(w, u"hover"_s, false);
      break;
    case QEvent::FocusIn:
      d_->setPseudo(w, u"focus"_s, true);
      break;
    case QEvent::FocusOut:
      d_->setPseudo(w, u"focus"_s, false);
      break;
    case QEvent::MouseButtonPress:
      if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) d_->onPress(w);
      break;
    case QEvent::MouseButtonRelease:
      d_->setPseudo(w, u"pressed"_s, false);
      break;
    case QEvent::DynamicPropertyChange: {
      const QByteArray name = static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName();
      if (name.startsWith('_')) break;
      if (const auto c = d_->ctxFor(w, true)) d_->evaluate(c);
      break;
    }
    case QEvent::ParentChange:
      if (const auto c = d_->ctxFor(w, true)) d_->evaluate(c);
      break;
    default:
      break;
  }
  return false;
}

}  // namespace yasb::css
