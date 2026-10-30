#include "features/color_scheme/tonal_palette.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace wabar::color_scheme {

namespace {

// Finds the key colour of a hue and requested chroma. Pivots around T50, which has the most chroma
// available on average, and binary-searches towards the tone whose maximum chroma first reaches the
// request.
class KeyColorSearch {
 public:
  KeyColorSearch(double hue, double requestedChroma)
      : hue_(hue), requestedChroma_(requestedChroma) {}

  Hct create() {
    constexpr int kPivotTone = 50;
    constexpr int kToneStep = 1;
    constexpr double kEpsilon = 0.01;
    int lower = 0;
    int upper = 100;
    while (lower < upper) {
      const int mid = (lower + upper) / 2;
      const bool ascending = maxChroma(mid) < maxChroma(mid + kToneStep);
      const bool sufficient = maxChroma(mid) >= requestedChroma_ - kEpsilon;
      if (sufficient) {
        if (std::abs(lower - kPivotTone) < std::abs(upper - kPivotTone)) {
          upper = mid;
        } else {
          if (lower == mid) return Hct(hue_, requestedChroma_, lower);
          lower = mid;
        }
      } else if (ascending) {
        lower = mid + kToneStep;
      } else {
        upper = mid;
      }
    }
    return Hct(hue_, requestedChroma_, lower);
  }

 private:
  double maxChroma(int tone) {
    auto it = cache_.find(tone);
    if (it != cache_.end()) return it->second;
    const double chroma = Hct(hue_, 200.0, tone).chroma();
    cache_[tone] = chroma;
    return chroma;
  }

  double hue_;
  double requestedChroma_;
  std::map<int, double> cache_;
};

}  // namespace

TonalPalette::TonalPalette(double hue, double chroma) : hue_(hue), chroma_(chroma) {
  keyColor_ = KeyColorSearch(hue, chroma).create();
}

TonalPalette::TonalPalette(const Hct& hct)
    : hue_(hct.hue()), chroma_(hct.chroma()), keyColor_(hct) {}

Argb TonalPalette::tone(double tone) const { return solveToArgb(hue_, chroma_, tone); }

TemperatureCache::TemperatureCache(const Hct& input) : input_(input) {
  hctsByHue_.reserve(362);
  for (int hue = 0; hue <= 360; ++hue) hctsByHue_.emplace_back(hue, input.chroma(), input.tone());
  hctsByHue_.push_back(input);
  temps_.reserve(hctsByHue_.size());
  for (const Hct& hct : hctsByHue_) temps_.push_back(rawTemperature(hct));
  // The reference sorts a stable list by temperature: coldest is the first minimum, warmest the
  // last maximum.
  for (size_t i = 0; i < temps_.size(); ++i) {
    if (temps_[i] < temps_[coldest_]) coldest_ = i;
    if (temps_[i] >= temps_[warmest_]) warmest_ = i;
  }
}

double TemperatureCache::rawTemperature(const Hct& hct) {
  const Lab lab = labFromArgb(hct.argb());
  const double hue = sanitizeDegrees(std::atan2(lab.b, lab.a) * 180.0 / kPi);
  const double chroma = std::hypot(lab.a, lab.b);
  return -0.5 + 0.02 * std::pow(chroma, 1.07) * std::cos(sanitizeDegrees(hue - 50.0) * kPi / 180);
}

double TemperatureCache::relativeTemperature(const Hct& hct) const {
  const double range = temps_[warmest_] - temps_[coldest_];
  // At T0 and T100 only one colour exists, so there is no range.
  if (range == 0.0) return 0.5;
  return (rawTemperature(hct) - temps_[coldest_]) / range;
}

bool TemperatureCache::isBetween(double angle, double a, double b) {
  if (a < b) return a <= angle && angle <= b;
  return a <= angle || angle <= b;
}

Hct TemperatureCache::complement() const {
  const Hct& coldest = hctsByHue_[coldest_];
  const Hct& warmest = hctsByHue_[warmest_];
  const double coldestTemp = temps_[coldest_];
  const double range = temps_[warmest_] - coldestTemp;
  const bool startIsColdestToWarmest = isBetween(input_.hue(), coldest.hue(), warmest.hue());
  const double startHue = startIsColdestToWarmest ? warmest.hue() : coldest.hue();
  const double endHue = startIsColdestToWarmest ? coldest.hue() : warmest.hue();
  double smallestError = 1000.0;
  Hct answer = hctsByHue_[static_cast<size_t>(std::lround(input_.hue()))];
  const double complementRelativeTemp = 1.0 - relativeTemperature(input_);
  for (double addend = 0.0; addend <= 360.0; addend += 1.0) {
    const double hue = sanitizeDegrees(startHue + addend);
    if (!isBetween(hue, startHue, endHue)) continue;
    const size_t index = static_cast<size_t>(std::lround(hue));
    const double relativeTemp = (temps_[index] - coldestTemp) / range;
    const double error = std::fabs(complementRelativeTemp - relativeTemp);
    if (error < smallestError) {
      smallestError = error;
      answer = hctsByHue_[index];
    }
  }
  return answer;
}

std::vector<Hct> TemperatureCache::analogousColors(int count, int divisions) const {
  const int startHue = static_cast<int>(std::lround(input_.hue()));
  const Hct startHct = hctsByHue_[static_cast<size_t>(startHue)];
  double lastTemp = relativeTemperature(startHct);
  std::vector<Hct> allColors{startHct};

  double absoluteTotalTempDelta = 0.0;
  for (int i = 0; i < 360; ++i) {
    const int hue = sanitizeDegreesInt(startHue + i);
    const double temp = relativeTemperature(hctsByHue_[static_cast<size_t>(hue)]);
    absoluteTotalTempDelta += std::fabs(temp - lastTemp);
    lastTemp = temp;
  }

  int hueAddend = 1;
  const double tempStep = absoluteTotalTempDelta / divisions;
  double totalTempDelta = 0.0;
  lastTemp = relativeTemperature(startHct);
  const size_t wanted = static_cast<size_t>(divisions);
  while (allColors.size() < wanted) {
    const int hue = sanitizeDegreesInt(startHue + hueAddend);
    const Hct& hct = hctsByHue_[static_cast<size_t>(hue)];
    const double temp = relativeTemperature(hct);
    totalTempDelta += std::fabs(temp - lastTemp);

    double desired = static_cast<double>(allColors.size()) * tempStep;
    bool satisfied = totalTempDelta >= desired;
    int indexAddend = 1;
    // A hue is added again while its temperature still covers the next step, so colours with no
    // analogues (white, black) fill the list.
    while (satisfied && allColors.size() < wanted) {
      allColors.push_back(hct);
      desired = static_cast<double>(allColors.size() + static_cast<size_t>(indexAddend)) * tempStep;
      satisfied = totalTempDelta >= desired;
      ++indexAddend;
    }
    lastTemp = temp;
    ++hueAddend;
    if (hueAddend > 360) {
      while (allColors.size() < wanted) allColors.push_back(hct);
      break;
    }
  }

  std::vector<Hct> answers{input_};
  const int ccwCount = static_cast<int>(std::floor((static_cast<double>(count) - 1.0) / 2.0));
  const int size = static_cast<int>(allColors.size());
  for (int i = 1; i < ccwCount + 1; ++i) {
    int index = -i;
    while (index < 0) index += size;
    index %= size;
    answers.insert(answers.begin(), allColors[static_cast<size_t>(index)]);
  }
  const int cwCount = count - ccwCount - 1;
  for (int i = 1; i < cwCount + 1; ++i) answers.push_back(allColors[static_cast<size_t>(i % size)]);
  return answers;
}

}  // namespace wabar::color_scheme
