#include "natural_time.h"

#include "core/widgets/label_format.h"

namespace yasb::util {

std::string naturalTimePhrase(long long seconds) { return yasb::widgets::naturalDelta(seconds); }

}  // namespace yasb::util
