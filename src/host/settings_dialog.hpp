#pragma once

#include "common/config.hpp"

#include <functional>
#include <windows.h>

namespace liteime::host {

enum class SettingsQuickCommand {
    None = 0,
    OpenQuickInput = 1,
    OpenCustomDictFolder = 2,
};

// Opens the non-blocking/modal-loop Settings Center dialog on the host UI thread.
// Calls `onApply` whenever the user clicks Apply or OK with modified configuration.
// Returns true if user accepted via OK/Apply at least once.
bool showSettingsCenterDialog(
    HWND parent,
    EngineConfig& inOutCfg,
    const std::function<void(const EngineConfig&)>& onApply,
    const std::function<void(SettingsQuickCommand)>& onCommand,
    HWND* outActiveHwnd = nullptr);

} // namespace liteime::host
