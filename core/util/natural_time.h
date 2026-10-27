#pragma once

#include <string>

namespace yasb::util {

// Humanize a non-negative duration using the phrases produced by YASB's humanize.naturaldelta.
std::string naturalTimePhrase(long long seconds);

}  // namespace yasb::util
