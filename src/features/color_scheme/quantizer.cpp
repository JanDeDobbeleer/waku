#include "features/color_scheme/quantizer.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_map>

#include "features/color_scheme/hct.h"

namespace wabar::color_scheme {

namespace {

// ---- Wu -------------------------------------------------------------------------------------

struct Box {
  int r0 = 0;
  int r1 = 0;
  int g0 = 0;
  int g1 = 0;
  int b0 = 0;
  int b1 = 0;
  int vol = 0;
};

enum class Direction { Red, Green, Blue };

constexpr int kIndexBits = 5;
constexpr int kIndexCount = (1 << kIndexBits) + 1;
constexpr size_t kTotalSize = static_cast<size_t>(kIndexCount) * kIndexCount * kIndexCount;
constexpr int kMaxColors = 256;

using IntArray = std::vector<std::int64_t>;
using DoubleArray = std::vector<double>;

size_t indexOf(int r, int g, int b) {
  return static_cast<size_t>((r << (kIndexBits * 2)) + (r << (kIndexBits + 1)) + (g << kIndexBits) +
                             r + g + b);
}

// The 3-D histogram of the pixels and its cumulative moments, which give any box's sums in O(1).
struct Moments {
  IntArray weights = IntArray(kTotalSize, 0);
  IntArray r = IntArray(kTotalSize, 0);
  IntArray g = IntArray(kTotalSize, 0);
  IntArray b = IntArray(kTotalSize, 0);
  DoubleArray squares = DoubleArray(kTotalSize, 0.0);
};

void constructHistogram(const std::vector<Argb>& pixels, Moments& m) {
  for (const Argb pixel : pixels) {
    const int red = redOf(pixel);
    const int green = greenOf(pixel);
    const int blue = blueOf(pixel);
    const int shift = 8 - kIndexBits;
    const size_t index = indexOf((red >> shift) + 1, (green >> shift) + 1, (blue >> shift) + 1);
    m.weights[index]++;
    m.r[index] += red;
    m.g[index] += green;
    m.b[index] += blue;
    m.squares[index] += red * red + green * green + blue * blue;
  }
}

void computeMoments(Moments& m) {
  for (int r = 1; r < kIndexCount; ++r) {
    std::int64_t area[kIndexCount] = {};
    std::int64_t areaR[kIndexCount] = {};
    std::int64_t areaG[kIndexCount] = {};
    std::int64_t areaB[kIndexCount] = {};
    double area2[kIndexCount] = {};
    for (int g = 1; g < kIndexCount; ++g) {
      std::int64_t line = 0;
      std::int64_t lineR = 0;
      std::int64_t lineG = 0;
      std::int64_t lineB = 0;
      double line2 = 0.0;
      for (int b = 1; b < kIndexCount; ++b) {
        const size_t index = indexOf(r, g, b);
        line += m.weights[index];
        lineR += m.r[index];
        lineG += m.g[index];
        lineB += m.b[index];
        line2 += m.squares[index];
        area[b] += line;
        areaR[b] += lineR;
        areaG[b] += lineG;
        areaB[b] += lineB;
        area2[b] += line2;
        const size_t previous = indexOf(r - 1, g, b);
        m.weights[index] = m.weights[previous] + area[b];
        m.r[index] = m.r[previous] + areaR[b];
        m.g[index] = m.g[previous] + areaG[b];
        m.b[index] = m.b[previous] + areaB[b];
        m.squares[index] = m.squares[previous] + area2[b];
      }
    }
  }
}

std::int64_t top(const Box& c, Direction d, int pos, const IntArray& m) {
  switch (d) {
    case Direction::Red:
      return m[indexOf(pos, c.g1, c.b1)] - m[indexOf(pos, c.g1, c.b0)] -
             m[indexOf(pos, c.g0, c.b1)] + m[indexOf(pos, c.g0, c.b0)];
    case Direction::Green:
      return m[indexOf(c.r1, pos, c.b1)] - m[indexOf(c.r1, pos, c.b0)] -
             m[indexOf(c.r0, pos, c.b1)] + m[indexOf(c.r0, pos, c.b0)];
    case Direction::Blue:
      break;
  }
  return m[indexOf(c.r1, c.g1, pos)] - m[indexOf(c.r1, c.g0, pos)] - m[indexOf(c.r0, c.g1, pos)] +
         m[indexOf(c.r0, c.g0, pos)];
}

std::int64_t bottom(const Box& c, Direction d, const IntArray& m) {
  switch (d) {
    case Direction::Red:
      return -m[indexOf(c.r0, c.g1, c.b1)] + m[indexOf(c.r0, c.g1, c.b0)] +
             m[indexOf(c.r0, c.g0, c.b1)] - m[indexOf(c.r0, c.g0, c.b0)];
    case Direction::Green:
      return -m[indexOf(c.r1, c.g0, c.b1)] + m[indexOf(c.r1, c.g0, c.b0)] +
             m[indexOf(c.r0, c.g0, c.b1)] - m[indexOf(c.r0, c.g0, c.b0)];
    case Direction::Blue:
      break;
  }
  return -m[indexOf(c.r1, c.g1, c.b0)] + m[indexOf(c.r1, c.g0, c.b0)] +
         m[indexOf(c.r0, c.g1, c.b0)] - m[indexOf(c.r0, c.g0, c.b0)];
}

template <typename T>
T vol(const Box& c, const std::vector<T>& m) {
  return m[indexOf(c.r1, c.g1, c.b1)] - m[indexOf(c.r1, c.g1, c.b0)] -
         m[indexOf(c.r1, c.g0, c.b1)] + m[indexOf(c.r1, c.g0, c.b0)] -
         m[indexOf(c.r0, c.g1, c.b1)] + m[indexOf(c.r0, c.g1, c.b0)] +
         m[indexOf(c.r0, c.g0, c.b1)] - m[indexOf(c.r0, c.g0, c.b0)];
}

double variance(const Box& c, const Moments& m) {
  const double dr = static_cast<double>(vol(c, m.r));
  const double dg = static_cast<double>(vol(c, m.g));
  const double db = static_cast<double>(vol(c, m.b));
  const double xx = vol(c, m.squares);
  const double hypotenuse = dr * dr + dg * dg + db * db;
  const double volume = static_cast<double>(vol(c, m.weights));
  return xx - hypotenuse / volume;
}

double maximize(const Box& c, Direction d, int first, int last, int* cut, std::int64_t wholeW,
                std::int64_t wholeR, std::int64_t wholeG, std::int64_t wholeB, const Moments& m) {
  const std::int64_t bottomR = bottom(c, d, m.r);
  const std::int64_t bottomG = bottom(c, d, m.g);
  const std::int64_t bottomB = bottom(c, d, m.b);
  const std::int64_t bottomW = bottom(c, d, m.weights);
  double max = 0.0;
  *cut = -1;
  for (int i = first; i < last; ++i) {
    std::int64_t halfR = bottomR + top(c, d, i, m.r);
    std::int64_t halfG = bottomG + top(c, d, i, m.g);
    std::int64_t halfB = bottomB + top(c, d, i, m.b);
    std::int64_t halfW = bottomW + top(c, d, i, m.weights);
    if (halfW == 0) continue;
    double temp = (static_cast<double>(halfR) * halfR + static_cast<double>(halfG) * halfG +
                   static_cast<double>(halfB) * halfB) /
                  static_cast<double>(halfW);
    halfR = wholeR - halfR;
    halfG = wholeG - halfG;
    halfB = wholeB - halfB;
    halfW = wholeW - halfW;
    if (halfW == 0) continue;
    temp += (static_cast<double>(halfR) * halfR + static_cast<double>(halfG) * halfG +
             static_cast<double>(halfB) * halfB) /
            static_cast<double>(halfW);
    if (temp > max) {
      max = temp;
      *cut = i;
    }
  }
  return max;
}

bool cutBox(Box& box1, Box& box2, const Moments& m) {
  const std::int64_t wholeR = vol(box1, m.r);
  const std::int64_t wholeG = vol(box1, m.g);
  const std::int64_t wholeB = vol(box1, m.b);
  const std::int64_t wholeW = vol(box1, m.weights);
  int cutR = 0;
  int cutG = 0;
  int cutB = 0;
  const double maxR = maximize(box1, Direction::Red, box1.r0 + 1, box1.r1, &cutR, wholeW, wholeR,
                               wholeG, wholeB, m);
  const double maxG = maximize(box1, Direction::Green, box1.g0 + 1, box1.g1, &cutG, wholeW, wholeR,
                               wholeG, wholeB, m);
  const double maxB = maximize(box1, Direction::Blue, box1.b0 + 1, box1.b1, &cutB, wholeW, wholeR,
                               wholeG, wholeB, m);
  Direction direction;
  if (maxR >= maxG && maxR >= maxB) {
    direction = Direction::Red;
    if (cutR < 0) return false;
  } else if (maxG >= maxR && maxG >= maxB) {
    direction = Direction::Green;
  } else {
    direction = Direction::Blue;
  }
  box2.r1 = box1.r1;
  box2.g1 = box1.g1;
  box2.b1 = box1.b1;
  switch (direction) {
    case Direction::Red:
      box2.r0 = box1.r1 = cutR;
      box2.g0 = box1.g0;
      box2.b0 = box1.b0;
      break;
    case Direction::Green:
      box2.r0 = box1.r0;
      box2.g0 = box1.g1 = cutG;
      box2.b0 = box1.b0;
      break;
    case Direction::Blue:
      box2.r0 = box1.r0;
      box2.g0 = box1.g0;
      box2.b0 = box1.b1 = cutB;
      break;
  }
  box1.vol = (box1.r1 - box1.r0) * (box1.g1 - box1.g0) * (box1.b1 - box1.b0);
  box2.vol = (box2.r1 - box2.r0) * (box2.g1 - box2.g0) * (box2.b1 - box2.b0);
  return true;
}

}  // namespace

std::vector<Argb> quantizeWu(const std::vector<Argb>& pixels, int maxColors) {
  if (maxColors <= 0 || maxColors > kMaxColors || pixels.empty()) return {};
  Moments m;
  constructHistogram(pixels, m);
  computeMoments(m);

  std::vector<Box> cubes(kMaxColors);
  cubes[0].r1 = cubes[0].g1 = cubes[0].b1 = kIndexCount - 1;
  std::vector<double> volumeVariance(kMaxColors, 0.0);
  int next = 0;
  for (int i = 1; i < maxColors; ++i) {
    if (cutBox(cubes[static_cast<size_t>(next)], cubes[static_cast<size_t>(i)], m)) {
      Box& a = cubes[static_cast<size_t>(next)];
      Box& b = cubes[static_cast<size_t>(i)];
      volumeVariance[static_cast<size_t>(next)] = a.vol > 1 ? variance(a, m) : 0.0;
      volumeVariance[static_cast<size_t>(i)] = b.vol > 1 ? variance(b, m) : 0.0;
    } else {
      volumeVariance[static_cast<size_t>(next)] = 0.0;
      --i;
    }
    next = 0;
    double best = volumeVariance[0];
    for (int j = 1; j <= i; ++j) {
      if (volumeVariance[static_cast<size_t>(j)] > best) {
        best = volumeVariance[static_cast<size_t>(j)];
        next = j;
      }
    }
    if (best <= 0.0) {
      maxColors = i + 1;
      break;
    }
  }

  std::vector<Argb> out;
  for (int i = 0; i < maxColors; ++i) {
    const Box& c = cubes[static_cast<size_t>(i)];
    const std::int64_t weight = vol(c, m.weights);
    if (weight > 0) {
      out.push_back(argbFromRgb(static_cast<int>(vol(c, m.r) / weight),
                                static_cast<int>(vol(c, m.g) / weight),
                                static_cast<int>(vol(c, m.b) / weight)));
    }
  }
  return out;
}

// ---- WSMeans --------------------------------------------------------------------------------

ColorCounts quantizeWsmeans(const std::vector<Argb>& inputPixels,
                            const std::vector<Argb>& startingClusters, int maxColors) {
  constexpr int kMaxIterations = 100;
  constexpr double kMinDeltaE = 3.0;
  if (maxColors <= 0 || inputPixels.empty()) return {};
  maxColors = std::min(maxColors, kMaxColors);

  // Distinct colours, in order of first appearance, with their counts.
  std::unordered_map<Argb, int> pixelToCount;
  std::vector<Argb> pixels;
  std::vector<Lab> points;
  for (const Argb pixel : inputPixels) {
    auto it = pixelToCount.find(pixel);
    if (it != pixelToCount.end()) {
      ++it->second;
    } else {
      pixels.push_back(pixel);
      points.push_back(labFromArgb(pixel));
      pixelToCount[pixel] = 1;
    }
  }

  int clusterCount = std::min(maxColors, static_cast<int>(points.size()));
  if (!startingClusters.empty()) {
    clusterCount = std::min(clusterCount, static_cast<int>(startingClusters.size()));
  }

  std::mt19937 random(42688);
  std::vector<Lab> clusters;
  for (const Argb argb : startingClusters) clusters.push_back(labFromArgb(argb));
  if (startingClusters.empty()) {
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    for (int i = 0; i < clusterCount; ++i) {
      clusters.push_back(
          {unit(random) * 100.0, unit(random) * 200.0 - 100.0, unit(random) * 200.0 - 100.0});
    }
  }

  std::vector<int> clusterIndices;
  clusterIndices.reserve(points.size());
  for (size_t i = 0; i < points.size(); ++i) {
    clusterIndices.push_back(static_cast<int>(random() % static_cast<unsigned>(clusterCount)));
  }

  const size_t n = static_cast<size_t>(clusterCount);
  std::vector<std::vector<double>> clusterDistance(n, std::vector<double>(n, 0.0));
  std::vector<int> pixelCountSums(n, 0);

  for (int iteration = 0; iteration < kMaxIterations; ++iteration) {
    for (size_t i = 0; i < n; ++i) {
      for (size_t j = i + 1; j < n; ++j) {
        const double distance = clusters[i].distanceSquared(clusters[j]);
        clusterDistance[i][j] = distance;
        clusterDistance[j][i] = distance;
      }
    }

    bool moved = false;
    for (size_t i = 0; i < points.size(); ++i) {
      const Lab& point = points[i];
      const size_t previousIndex = static_cast<size_t>(clusterIndices[i]);
      const double previousDistance = point.distanceSquared(clusters[previousIndex]);
      double minimumDistance = previousDistance;
      int newIndex = -1;
      for (size_t j = 0; j < n; ++j) {
        // A cluster more than twice as far from the current one as the point is cannot be nearer.
        if (clusterDistance[previousIndex][j] >= 4 * previousDistance) continue;
        const double distance = point.distanceSquared(clusters[j]);
        if (distance < minimumDistance) {
          minimumDistance = distance;
          newIndex = static_cast<int>(j);
        }
      }
      if (newIndex != -1 &&
          std::fabs(std::sqrt(minimumDistance) - std::sqrt(previousDistance)) > kMinDeltaE) {
        moved = true;
        clusterIndices[i] = newIndex;
      }
    }
    if (!moved && iteration != 0) break;

    std::vector<double> sumL(n, 0.0);
    std::vector<double> sumA(n, 0.0);
    std::vector<double> sumB(n, 0.0);
    std::fill(pixelCountSums.begin(), pixelCountSums.end(), 0);
    for (size_t i = 0; i < points.size(); ++i) {
      const size_t cluster = static_cast<size_t>(clusterIndices[i]);
      const int count = pixelToCount[pixels[i]];
      pixelCountSums[cluster] += count;
      sumL[cluster] += points[i].l * count;
      sumA[cluster] += points[i].a * count;
      sumB[cluster] += points[i].b * count;
    }
    for (size_t i = 0; i < n; ++i) {
      const int count = pixelCountSums[i];
      clusters[i] =
          count == 0 ? Lab{0, 0, 0} : Lab{sumL[i] / count, sumA[i] / count, sumB[i] / count};
    }
  }

  // Merge clusters that land on the same sRGB colour; order by population (stable on cluster
  // order).
  struct Swatch {
    Argb argb;
    int population;
  };
  std::vector<Swatch> swatches;
  for (size_t i = 0; i < n; ++i) {
    const Argb argb = argbFromLab(clusters[i]);
    const int count = pixelCountSums[i];
    if (count == 0) continue;
    auto same = std::find_if(swatches.begin(), swatches.end(),
                             [argb](const Swatch& s) { return s.argb == argb; });
    if (same != swatches.end()) {
      same->population += count;
    } else {
      swatches.push_back({argb, count});
    }
  }
  ColorCounts result;
  for (const Swatch& s : swatches) result[s.argb] = static_cast<std::uint32_t>(s.population);
  return result;
}

ColorCounts quantizeCelebi(const std::vector<Argb>& pixels, int maxColors) {
  if (maxColors <= 0 || pixels.empty()) return {};
  maxColors = std::min(maxColors, kMaxColors);
  std::vector<Argb> opaque;
  opaque.reserve(pixels.size());
  for (const Argb pixel : pixels) {
    if (isOpaque(pixel)) opaque.push_back(pixel);
  }
  const std::vector<Argb> wu = quantizeWu(opaque, maxColors);
  return quantizeWsmeans(opaque, wu, maxColors);
}

// ---- Score ----------------------------------------------------------------------------------

std::vector<Argb> rankedSuggestions(const ColorCounts& colors, const ScoreOptions& options) {
  constexpr double kTargetChroma = 48.0;
  constexpr double kWeightProportion = 0.7;
  constexpr double kWeightChromaAbove = 0.3;
  constexpr double kWeightChromaBelow = 0.1;
  constexpr double kCutoffChroma = 5.0;
  constexpr double kCutoffExcitedProportion = 0.01;

  std::vector<Hct> colorsHct;
  std::vector<double> huePopulation(360, 0.0);
  double populationSum = 0;
  for (const auto& [argb, population] : colors) {
    const Hct hct(argb);
    colorsHct.push_back(hct);
    huePopulation[static_cast<size_t>(std::floor(hct.hue())) % 360] += population;
    populationSum += population;
  }

  // Hues used a lot in the neighbouring 30 degrees get a larger share.
  std::vector<double> excitedProportions(360, 0.0);
  for (int hue = 0; hue < 360; ++hue) {
    const double proportion = huePopulation[static_cast<size_t>(hue)] / populationSum;
    for (int i = hue - 14; i < hue + 16; ++i) {
      excitedProportions[static_cast<size_t>(sanitizeDegreesInt(i))] += proportion;
    }
  }

  std::vector<std::pair<Hct, double>> scored;
  for (const Hct& hct : colorsHct) {
    const int hue = sanitizeDegreesInt(static_cast<int>(std::round(hct.hue())));
    const double proportion = excitedProportions[static_cast<size_t>(hue)];
    if (options.filter &&
        (hct.chroma() < kCutoffChroma || proportion <= kCutoffExcitedProportion)) {
      continue;
    }
    const double proportionScore = proportion * 100.0 * kWeightProportion;
    const double chromaWeight =
        hct.chroma() < kTargetChroma ? kWeightChromaBelow : kWeightChromaAbove;
    const double chromaScore = (hct.chroma() - kTargetChroma) * chromaWeight;
    scored.emplace_back(hct, proportionScore + chromaScore);
  }
  std::stable_sort(scored.begin(), scored.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });

  // Take colours with hues at least `difference` degrees apart, starting at 90 and relaxing to 15,
  // until enough are found.
  std::vector<Hct> chosen;
  for (int difference = 90; difference >= 15; --difference) {
    chosen.clear();
    for (const auto& entry : scored) {
      const Hct& hct = entry.first;
      const bool duplicate = std::any_of(chosen.begin(), chosen.end(), [&](const Hct& other) {
        return diffDegrees(hct.hue(), other.hue()) < difference;
      });
      if (!duplicate) {
        chosen.push_back(hct);
        if (chosen.size() >= options.desired) break;
      }
    }
    if (chosen.size() >= options.desired) break;
  }

  std::vector<Argb> result;
  if (chosen.empty()) result.push_back(options.fallback);
  for (const Hct& hct : chosen) result.push_back(hct.argb());
  return result;
}

}  // namespace wabar::color_scheme
