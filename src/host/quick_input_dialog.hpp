#pragma once

#include "engine/user_dict.hpp"
#include <windows.h>
#include <vector>

namespace liteime::host {

// Displays the mandatory privacy warning first; if confirmed, opens a local
// manager dialog to edit "vvv" QuickInput items in-place without holding engine locks.
// Returns true if user accepted edits.
bool editQuickInputItems(HWND owner, std::vector<liteime::engine::QuickInputItem>& items);

// Convenience wrapper that loads from and saves directly to userDict.
bool showQuickInputDialog(HWND owner, liteime::engine::UserDict& userDict);

} // namespace liteime::host
