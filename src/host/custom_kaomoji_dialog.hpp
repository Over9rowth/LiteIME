#pragma once

#include "engine/user_dict.hpp"
#include <windows.h>
#include <vector>

namespace liteime::host {

bool editCustomKaomojiItems(HWND owner,
                            std::vector<liteime::engine::CustomKaomojiItem>& items,
                            const std::filesystem::path& filePath);

bool showCustomKaomojiDialog(HWND owner, liteime::engine::UserDict& userDict);

} // namespace liteime::host
