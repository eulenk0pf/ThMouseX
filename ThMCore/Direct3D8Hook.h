#pragma once
#include "framework.h"
#include <vector>

#include "../Common/MinHook.h"
#include "macro.h"

namespace core::directx8hook {
    using CallbackType = void (*)(void);
    void RegisterPostRenderCallbacks(CallbackType callback);
    bool PopulateMethodRVAs();
    std::vector<common::minhook::HookConfig> HookConfig();
    void ClearMeasurementFlags();
}