#include "widgets/wallpapers/gallery_layout.h"

#include <algorithm>
#include <cmath>

namespace yasb::widgets {
namespace {

int pyMod(int value, int divisor) {
  const int r = value % divisor;
  return (r != 0 && (r < 0) != (divisor < 0)) ? r + divisor : r;
}

class DefaultStyle final : public CardStyle {
 public:
  static constexpr int kGap = 6;
  double stepPixels(int cardWidth) const override { return cardWidth + kGap; }
  int border() const override { return 2; }
  Placement place(double distance, int cardWidth) const override {
    Placement p;
    p.x = distance * stepPixels(cardWidth);
    return p;
  }
};

class MagnifiedStyle final : public CardStyle {
 public:
  static constexpr int kGap = 6;
  static constexpr double kGrow = 0.2;
  static constexpr double kReach = 1.0;

  int border() const override { return 2; }
  double maxScale() const override { return 1.0 + kGrow; }

  Placement place(double distance, int cardWidth) const override {
    Placement p;
    p.x = distance * stepPixels(cardWidth);
    p.scale = scaleAt(distance);
    return p;
  }

  Placement selectedSpot(int) const override {
    Placement p;
    p.scale = maxScale();
    return p;
  }

  std::vector<IndexedPlacement> layoutCards(const std::vector<VisibleCard>& visible,
                                            int cardWidth) const override {
    std::vector<VisibleCard> cards = visible;
    std::stable_sort(cards.begin(), cards.end(), [](const VisibleCard& a, const VisibleCard& b) {
      return a.distance < b.distance;
    });
    if (cards.empty()) return {};

    std::vector<double> widths;
    widths.reserve(cards.size());
    for (const VisibleCard& card : cards) widths.push_back(cardWidth * scaleAt(card.distance));

    // Accumulated, so growth pushes the neighbours aside instead of covering them. A closed form
    // of this gets the spacing wrong mid-move.
    std::vector<double> centres{0.0};
    for (size_t i = 0; i + 1 < widths.size(); ++i)
      centres.push_back(centres.back() + (widths[i] + widths[i + 1]) / 2.0 + kGap);

    double anchor = centres.front();
    for (size_t i = 0; i + 1 < cards.size(); ++i) {
      if (cards[i].distance <= 0.0 && 0.0 <= cards[i + 1].distance) {
        const double span = cards[i + 1].distance - cards[i].distance;
        const double share = span != 0.0 ? (0.0 - cards[i].distance) / span : 0.0;
        anchor = centres[i] + (centres[i + 1] - centres[i]) * share;
        break;
      }
    }

    std::vector<IndexedPlacement> placed;
    placed.reserve(cards.size());
    for (size_t i = 0; i < cards.size(); ++i) {
      Placement p;
      p.x = centres[i] - anchor;
      p.scale = scaleAt(cards[i].distance);
      p.focus = std::max(0.0, 1.0 - std::abs(cards[i].distance));
      placed.push_back({cards[i].index, p});
    }
    std::stable_sort(placed.begin(), placed.end(),
                     [](const IndexedPlacement& a, const IndexedPlacement& b) {
                       return a.spot.focus < b.spot.focus;
                     });
    return placed;
  }

 private:
  static double scaleAt(double distance) {
    return 1.0 + kGrow * std::max(0.0, 1.0 - std::abs(distance) / kReach);
  }
};

class StripStyle final : public CardStyle {
 public:
  static constexpr double kLean = 0.21;  // of the card edges, in half card heights
  int border() const override { return 2; }
  double dim() const override { return 0.40; }
  Placement place(double distance, int cardWidth) const override {
    Placement p;
    p.x = distance * stepPixels(cardWidth);
    p.shear = kLean;
    return p;
  }
};

class SlideStyle final : public CardStyle {
 public:
  static constexpr double kStep = 0.72;
  static constexpr double kShrink = 0.26;
  static constexpr double kFade = 0.28;
  double stepPixels(int cardWidth) const override { return kStep * cardWidth; }
  Placement place(double distance, int cardWidth) const override {
    const double span = std::abs(distance);
    Placement p;
    p.x = distance * stepPixels(cardWidth);
    p.scale = 1.0 / (1.0 + kShrink * span);
    p.opacity = std::max(0.0, 1.0 - span * kFade);
    return p;
  }
};

}  // namespace

double pyFmod(double value, double divisor) {
  double r = std::fmod(value, divisor);
  if (r != 0.0 && ((r < 0.0) != (divisor < 0.0))) r += divisor;
  return r;
}

double relativeDistance(int index, double offset, int count, bool wraps) {
  double distance = index - offset;
  if (wraps && count > 1) distance = pyFmod(distance + count / 2.0, count) - count / 2.0;
  return distance;
}

std::vector<VisibleCard> visibleCards(double offset, int count, int neighbours, bool wraps) {
  if (count <= 0) return {};
  const double limit = neighbours + 0.5;
  const int centre = static_cast<int>(std::floor(offset + 0.5));
  // Insertion-ordered, like the Python dict: a repeat index keeps its slot and its nearer distance.
  std::vector<VisibleCard> nearest;
  for (int step = centre - neighbours - 1; step < centre + neighbours + 2; ++step) {
    const int index = pyMod(step, count);
    const double distance = relativeDistance(index, offset, count, wraps);
    if (std::abs(distance) > limit) continue;
    auto it = std::find_if(nearest.begin(), nearest.end(),
                           [&](const VisibleCard& c) { return c.index == index; });
    if (it == nearest.end())
      nearest.push_back({index, distance});
    else if (std::abs(distance) < std::abs(it->distance))
      it->distance = distance;
  }
  std::stable_sort(nearest.begin(), nearest.end(), [](const VisibleCard& a, const VisibleCard& b) {
    return -std::abs(a.distance) < -std::abs(b.distance);
  });
  return nearest;
}

FitResult fitToView(int viewWidth, double stepPixels, int count) {
  const double step = std::max(1.0, stepPixels);
  const int needed = static_cast<int>(std::ceil(viewWidth / (2.0 * step))) + 1;
  FitResult result;
  result.neighbours = std::max(1, std::min(needed, std::max(1, (count - 1) / 2)));
  result.wraps = count > 2 * result.neighbours;
  return result;
}

double normaliseOffset(double offset, int count, bool wraps) {
  return (count > 0 && wraps) ? pyFmod(offset, count) : offset;
}

int pageStepFor(int viewWidth, int cardWidth, double stepPixels) {
  const double step = std::max(1.0, stepPixels);
  return std::max(1, static_cast<int>(std::floor((viewWidth / 2.0 - cardWidth / 2.0) / step)));
}

QPolygonF projectCard(const Placement& spot, QSize card, QSize view, double inset) {
  const double halfW = std::max(1.0, card.width() * spot.scale / 2.0 - inset);
  const double halfH = std::max(1.0, card.height() * spot.scale / 2.0 - inset);
  const double centreX = view.width() / 2.0 + spot.x;
  const double centreY = view.height() / 2.0;
  const double lean = spot.shear * halfH;
  return QPolygonF({QPointF(centreX - halfW + lean, centreY - halfH),
                    QPointF(centreX + halfW + lean, centreY - halfH),
                    QPointF(centreX + halfW - lean, centreY + halfH),
                    QPointF(centreX - halfW - lean, centreY + halfH)});
}

QSize cardSizeFor(int imageWidth, const QString& orientation) {
  const int height =
      orientation == QLatin1String("landscape") ? imageWidth * 9 / 16 : imageWidth * 16 / 9;
  return QSize(imageWidth, height);
}

std::vector<IndexedPlacement> CardStyle::layoutCards(const std::vector<VisibleCard>& visible,
                                                     int cardWidth) const {
  std::vector<IndexedPlacement> cards;
  cards.reserve(visible.size());
  for (const VisibleCard& card : visible) {
    Placement spot = place(card.distance, cardWidth);
    spot.focus = std::max(0.0, 1.0 - std::abs(card.distance));
    cards.push_back({card.index, spot});
  }
  return cards;
}

std::unique_ptr<CardStyle> makeCardStyle(const QString& type) {
  if (type == QLatin1String("magnified")) return std::make_unique<MagnifiedStyle>();
  if (type == QLatin1String("strip")) return std::make_unique<StripStyle>();
  if (type == QLatin1String("slide")) return std::make_unique<SlideStyle>();
  return std::make_unique<DefaultStyle>();
}

}  // namespace yasb::widgets
