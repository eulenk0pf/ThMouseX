module;

#include "framework.h"
#include "macro.h"
#include <vector>
#include <Psapi.h>
#include <tlhelp32.h>
#include <string_view>
#include <string>
#include <regex>
#include <tuple>

export module common.helper;

import common.var;
import common.datatype;
import common.luajit;
import common.neolua;
import common.helper.memory;

namespace luajit = common::luajit;
namespace neolua = common::neolua;
namespace memory = common::helper::memory;

using namespace std;

wregex whitespace(LR"([^\s])", wregex::ECMAScript | wregex::optimize);

namespace common::helper {
    export DLLEXPORT void ReportLastError(const char* title) {
        auto flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        auto dwErr = GetLastError();
        PSTR errorMessage{};
        FormatMessageA(flags, NULL, dwErr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), PSTR(&errorMessage), 0, NULL);
        MessageBoxA(NULL, errorMessage, title, MB_OK | MB_ICONERROR);
        LocalFree(errorMessage);
    }

    export DLLEXPORT string_view LTrim(string_view str) {
        match_results<string_view::const_iterator> match;
        auto pos = regex_search(str.cbegin(), str.cend(), match, whitespace) ? match.position(0) : string::npos;
        str.remove_prefix(min(pos, str.length()));
        return str;
    }

    export DLLEXPORT string_view RTrim(string_view str) {
        match_results<reverse_iterator<string_view::const_iterator>> match;
        auto pos = regex_search(str.crbegin(), str.crend(), match, whitespace) ? match.position(0) : string::npos;
        str.remove_suffix(min(pos, str.length()));
        return str;
    }

    export DLLEXPORT string_view Trim(string_view str) {
        return RTrim(LTrim(str));
    }

    export DLLEXPORT void CalculateNextModulate(UCHAR& modulate, ModulateStage& modulateStage) {
        constexpr UCHAR Delta = 16;
        constexpr UCHAR WhiteIntensityLimit = 128;
        constexpr UCHAR BlackIntensityLimit = 16;
        switch (modulateStage) {
            case WhiteInc:
                if (modulate == WhiteIntensityLimit) {
                    modulateStage = WhiteDec;
                    goto WhiteDec;
                } else {
                    modulate += Delta;
                }
                break;
            case WhiteDec: WhiteDec:
                if (modulate == 0) {
                    modulateStage = BlackInc;
                    modulate = BlackIntensityLimit;
                } else {
                    modulate -= Delta;
                }
                break;
            case BlackInc:
                if (modulate == 0) {
                    modulateStage = BlackDec;
                    goto BlackDec;
                } else {
                    modulate -= Delta;
                }
                break;
            case BlackDec: BlackDec:
                if (modulate == BlackIntensityLimit) {
                    modulateStage = WhiteInc;
                    modulate = 0;
                } else {
                    modulate += Delta;
                }
                break;
        }
    }

    export DLLEXPORT POINT GetPointerPosition() {
        POINT pointerPosition;
        GetCursorPos(&pointerPosition);
        ScreenToClient(g_hFocusWindow, &pointerPosition);
        return pointerPosition;
    }

    export DLLEXPORT void RemoveWindowBorder(UINT width, UINT height) {
        auto style = GetWindowLongPtrW(g_hFocusWindow, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_SIZEBOX | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        auto lExStyle = GetWindowLongPtrW(g_hFocusWindow, GWL_EXSTYLE);
        lExStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
        SetWindowLongPtrW(g_hFocusWindow, GWL_STYLE, style);
        SetWindowLongPtrW(g_hFocusWindow, GWL_EXSTYLE, lExStyle);
        SetWindowPos(g_hFocusWindow, NULL, 0, 0, width, height, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }

    export DLLEXPORT
        void FixWindowCoordinate(bool isExclusiveMode, UINT d3dWidth, UINT d3dHeight, UINT clientWidth, UINT clientHeight) {
        if (isExclusiveMode) {
            auto style = GetWindowLongPtrW(g_hFocusWindow, GWL_STYLE);
            style &= ~(WS_CAPTION | WS_SIZEBOX | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
            auto exStyle = GetWindowLongPtrW(g_hFocusWindow, GWL_EXSTYLE);
            exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
            SetWindowLongPtrW(g_hFocusWindow, GWL_STYLE, style);
            SetWindowLongPtrW(g_hFocusWindow, GWL_EXSTYLE, exStyle);
            auto updateFlags = SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER;
            SetWindowPos(g_hFocusWindow, NULL, 0, 0, d3dWidth, d3dHeight, updateFlags);
        } else if (d3dWidth > clientWidth || d3dHeight > clientHeight) {
            // fix for Touhou 18
            RECTSIZE size{0, 0, LONG(d3dWidth), LONG(d3dHeight)};
            auto style = GetWindowLongPtrW(g_hFocusWindow, GWL_STYLE);
            auto hasMenu = GetMenu(g_hFocusWindow) != NULL ? TRUE : FALSE;
            auto exStyle = GetWindowLongPtrW(g_hFocusWindow, GWL_EXSTYLE);
            AdjustWindowRectEx(&size, style, hasMenu, exStyle);
            auto updateFlags = SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOREPOSITION;
            SetWindowPos(g_hFocusWindow, NULL, 0, 0, size.width(), size.height(), updateFlags);
        }
    }

    // use for directx8
    export DLLEXPORT bool TestFullscreenHeuristically() {
        MONITORINFO monitorInfo{
            .cbSize = sizeof(MONITORINFO),
        };
        GetMonitorInfoW(MonitorFromWindow(g_hFocusWindow, MONITOR_DEFAULTTOPRIMARY), &monitorInfo);

        RECT hwndRect;
        GetWindowRect(g_hFocusWindow, &hwndRect);

        return hwndRect.left == monitorInfo.rcMonitor.left
            && hwndRect.right == monitorInfo.rcMonitor.right
            && hwndRect.top == monitorInfo.rcMonitor.top
            && hwndRect.bottom == monitorInfo.rcMonitor.bottom;
    }

    export DLLEXPORT DWORD CalculateAddress() {
        if (g_currentConfig.ScriptingMethodToFindAddress == ScriptingMethod::LuaJIT) {
            return luajit::GetPositionAddress();
        } else if (g_currentConfig.ScriptingMethodToFindAddress == ScriptingMethod::NeoLua) {
            return neolua::GetPositionAddress();
        } else {
            return memory::ResolveAddress(g_currentConfig.Address.Level, g_currentConfig.Address.Length);
        }
    }
}
