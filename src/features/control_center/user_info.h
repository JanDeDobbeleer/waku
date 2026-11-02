#pragma once

#include <QString>

namespace waku::control_center {

// services/power_menu/user_info.py. Both are cached after the first call.

// Windows display name of the current user, else the account name.
QString windowsUsername();
// Largest account picture for the current user, else the default user-192.png; "" when none.
QString userAvatarPath();

}  // namespace waku::control_center
