#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "features/color_scheme/color_math.h"

namespace waku::color_scheme {

// Colours found in an image, each with the number of pixels it stands for.
using ColorCounts = std::map<Argb, std::uint32_t>;

// Wu's quantizer: splits the RGB histogram into at most `maxColors` boxes and returns each box's
// mean colour. Fast and deterministic; Celebi uses it to seed k-means.
std::vector<Argb> quantizeWu(const std::vector<Argb>& pixels, int maxColors);

// K-means in L*a*b* from `startingClusters`. The reference library seeds the pixel-to-cluster
// assignment with rand(); this port uses a fixed-seed generator so a run is repeatable.
ColorCounts quantizeWsmeans(const std::vector<Argb>& pixels,
                            const std::vector<Argb>& startingClusters, int maxColors);

// Celebi's method: Wu, then k-means. Transparent pixels are ignored. At most 256 colours.
ColorCounts quantizeCelebi(const std::vector<Argb>& pixels, int maxColors);

struct ScoreOptions {
  size_t desired = 4;
  Argb fallback = 0xff4285f4;  // Google blue, used when no colour qualifies
  bool filter = true;          // drop colours with little chroma or few similar-hue pixels
};

// Ranks quantized colours by how well they suit as a theme source: hue popularity weighted with
// chroma, then picked so the chosen hues differ as much as possible. Returns at most
// `options.desired` colours, best first; the fallback alone when nothing qualifies.
std::vector<Argb> rankedSuggestions(const ColorCounts& colors, const ScoreOptions& options = {});

}  // namespace waku::color_scheme
