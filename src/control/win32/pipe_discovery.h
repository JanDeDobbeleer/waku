#pragma once

#include <QStringList>

namespace waku::control {

QStringList findPipesWithPrefix(const QString& prefix, QString* error);

}  // namespace waku::control
