module;

#include "framework.h"
#include <mmsystem.h>
#include <vector>

export module core.lowlevelinputhook;

import common.minhook;
import core.inputdeterminte;
import common.var;
import common.datatype;

using namespace std;

BOOL WINAPI _GetKeyboardState(PBYTE lpKeyState);
decltype(&_GetKeyboardState) OriGetKeyboardState;

export vector<MHookApiConfig> LowLevelInputHookConfig() {
    if (g_currentConfig.InputMethod != InputMethod::GetKeyboardState)
        return {};
    return {
        {L"USER32.DLL", "GetKeyboardState", &_GetKeyboardState, (PVOID*)&OriGetKeyboardState},
    };
};

BOOL WINAPI _GetKeyboardState(PBYTE lpKeyState) {
    auto rs = OriGetKeyboardState(lpKeyState);
    if (rs != FALSE) {
        auto gameInput = DetermineGameInput();
        if (gameInput & USE_BOMB)
            lpKeyState[gs_bombButton] |= 0x80;
        if (gameInput & USE_SPECIAL)
            lpKeyState[gs_extraButton] |= 0x80;
        if (gameInput & MOVE_LEFT)
            lpKeyState[VK_LEFT] |= 0x80;
        if (gameInput & MOVE_RIGHT)
            lpKeyState[VK_RIGHT] |= 0x80;
        if (gameInput & MOVE_UP)
            lpKeyState[VK_UP] |= 0x80;
        if (gameInput & MOVE_DOWN)
            lpKeyState[VK_DOWN] |= 0x80;
    }
    return rs;
}
