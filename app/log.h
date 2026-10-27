#pragma once

#include <QString>
#include <functional>

namespace yasb::app {

bool initializeLog(const QString& path, bool debug);
void setLogObserver(std::function<void(const QString&)> observer);

}  // namespace yasb::app
