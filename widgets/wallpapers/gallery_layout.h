#pragma once

#include <QPolygonF>
#include <QSize>
#include <QString>
#include <memory>
#include <utility>
#include <vector>

namespace yasb::widgets {

// Pure layout maths of the wallpaper galleries (services/wallpapers/gallery.py): where each card
// sits for a given scroll offset. No widgets, no painting.

// Where one card sits, in pixels from the centre of the view.
struct Placement {
  double x = 0.0;
  double scale = 1.0;
  double shear = 0.0;
  double opacity = 1.0;
  double focus = 0.0;
};

struct VisibleCard {
  int index;
  double distance;
};

struct IndexedPlacement {
  int index;
  Placement spot;
};

// Python's % on floats: the result takes the sign of the divisor.
double pyFmod(double value, double divisor);

// Distance in cards from the scroll offset to card `index`, wrapping when the row loops.
double relativeDistance(int index, double offset, int count, bool wraps);

// The cards close enough to the offset to draw, farthest first.
std::vector<VisibleCard> visibleCards(double offset, int count, int neighbours, bool wraps);

struct FitResult {
  int neighbours = 1;
  bool wraps = false;
};
// How many cards each side the view needs, and whether the row wraps around.
FitResult fitToView(int viewWidth, double stepPixels, int count);

double normaliseOffset(double offset, int count, bool wraps);

// How far Page Up/Down moves: onto the last card actually on screen.
int pageStepFor(int viewWidth, int cardWidth, double stepPixels);

// The four corners of a card, leaning by `spot.shear`, shrunk by `inset` on every side.
QPolygonF projectCard(const Placement& spot, QSize card, QSize view, double inset = 0.0);

// width x (9/16 or 16/9 of width), integer division as in Python.
QSize cardSizeFor(int imageWidth, const QString& orientation);

// One gallery type: default, magnified, strip or slide.
class CardStyle {
 public:
  virtual ~CardStyle() = default;

  // Distance between neighbouring card centres, in pixels.
  virtual double stepPixels(int cardWidth) const { return cardWidth; }
  // Frame every card gives up, painted only on the selected one.
  virtual int border() const { return 0; }
  // How far the unselected cards are darkened (0..1).
  virtual double dim() const { return 0.0; }
  // Largest a card ever gets, so the window can be tall enough.
  virtual double maxScale() const { return 1.0; }

  virtual Placement place(double distance, int cardWidth) const = 0;
  // Where the selected card sits, the largest a card is ever drawn.
  virtual Placement selectedSpot(int cardWidth) const { return place(0.0, cardWidth); }
  // Every card worth painting, outermost first.
  virtual std::vector<IndexedPlacement> layoutCards(const std::vector<VisibleCard>& visible,
                                                    int cardWidth) const;
};

// Unknown names fall back to "default" (the config schema only allows the four).
std::unique_ptr<CardStyle> makeCardStyle(const QString& type);

}  // namespace yasb::widgets
