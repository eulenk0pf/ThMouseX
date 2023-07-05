#include "framework.h"
#include <string>
#include <codecvt>
#include "macro.h"
#include "luajit/lua.hpp"

#include "MinHook.h"
#include "LuaJIT.h"
#include "Log.h"
#include "Helper.Memory.h"
#include "Helper.Encoding.h"
#include "Variables.h"

namespace note = common::log;
namespace memory = common::helper::memory;
namespace encoding = common::helper::encoding;
namespace minhook = common::minhook;

using namespace std;

#define Common_Lib_Path "%Common_Lib_Path%"

DWORD Common_LuaJIT_ReadUInt32(DWORD address) {
    return *PDWORD(address);
}

DWORD Common_LuaJIT_ResolveAddress(DWORD* offsets, int length) {
    return memory::ResolveAddress(offsets, length);
}

void Common_LuaJIT_OpenConsole() {
    note::OpenConsole();
}

wstring MakeCommonDllPathW() {
    return wstring(g_currentModuleDirPath) + L"\\" + L"Common.dll";
}

string GetPreparationScript() {
    auto keyword = Common_Lib_Path;
    auto preparationScript = string(R"(
        local ffi = require("ffi")

        ffi.cdef [[
            uint32_t Common_LuaJIT_ReadUInt32     (uint32_t address);
            uint32_t Common_LuaJIT_ResolveAddress (uint32_t* offsets, int length);
            void     Common_LuaJIT_OpenConsole    ();
        ]]

        local ThMouseX = ffi.load()" Common_Lib_Path R"()

        function OpenConsole()
            return ThMouseX.Common_LuaJIT_OpenConsole()
        end

        function ReadUInt32(address)
            return ThMouseX.Common_LuaJIT_ReadUInt32(address)
        end

        function ResolveAddress(addressChain, length)
            return ThMouseX.Common_LuaJIT_ResolveAddress(addressChain, length)
        end

        function AllocNew(...)
            return ffi.new(unpack({...}))
        end
    )");
    auto keywordPos = preparationScript.find(keyword);
    auto keywordLen = strlen(keyword);
    auto commonDllPath = encoding::ConvertToUtf8(MakeCommonDllPathW().c_str());
    preparationScript.replace(keywordPos, keywordLen, commonDllPath);
    return preparationScript;
}

bool scriptingDisabled = false;

#define GET_POSITION_ADDRESS "getPositionAddress"

lua_State* L;

bool CheckAndDisableIfError(lua_State* L, int r) {
    if (r != 0) {
        note::ToFile("[LuaJIT] %s", lua_tostring(L, -1));
        scriptingDisabled = true;
        return false;
    }
    return true;
}

namespace common::luajit {
    HMODULE WINAPI _LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
    decltype(&_LoadLibraryExA) OriLoadLibraryExA;

    vector<minhook::HookApiConfig> HookConfig = {
        {L"KERNEL32.DLL", "LoadLibraryExA", &_LoadLibraryExA, (PVOID*)&OriLoadLibraryExA},
    };

    HMODULE WINAPI _LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
        auto commonDllPath = MakeCommonDllPathW();
        if (strcmp(lpLibFileName, encoding::ConvertToUtf8(commonDllPath.c_str()).c_str()))
            return LoadLibraryExW(commonDllPath.c_str(), hFile, dwFlags);
        else
            return OriLoadLibraryExA(lpLibFileName, hFile, dwFlags);
    }

    void Initialize() {
        if (g_currentConfig.ScriptingMethodToFindAddress != ScriptingMethod::LuaJIT)
            return;

        L = luaL_newstate();
        if (L == NULL) {
            note::ToFile("[LuaJIT] %s", "Failed to initialize LuaJIT.");
            scriptingDisabled = true;
            return;
        }

        luaL_openlibs(L);

        if (!minhook::CreateApiHook(HookConfig)) {
            note::ToFile("[LuaJIT] Failed to hook LoadLibraryExA.");
            return;
        }

        if (!minhook::EnableHooks(HookConfig)) {
            note::ToFile("[LuaJIT] Failed to hook LoadLibraryExA.");
            return;
        }

        if (!CheckAndDisableIfError(L, luaL_dostring(L, GetPreparationScript().c_str()))) {
            minhook::DisableHooks(HookConfig);
            note::ToFile("[LuaJIT] The above error occurred in PreparationScript.");
            return;
        }

        minhook::DisableHooks(HookConfig);

        auto wScriptPath = wstring(g_currentModuleDirPath) + L"/ConfigScripts/" + g_currentConfig.ProcessName + L".lua";
        auto scriptPath = encoding::ConvertToUtf8(wScriptPath.c_str());

        if (!CheckAndDisableIfError(L, luaL_dofile(L, scriptPath.c_str())))
            return;

        lua_getglobal(L, GET_POSITION_ADDRESS);
        if (!lua_isfunction(L, -1)) {
            note::ToFile("[LuaJIT] %s", GET_POSITION_ADDRESS " function not found in global scope.");
            scriptingDisabled = true;
            return;
        }
    }

    DWORD GetPositionAddress() {
        if (scriptingDisabled)
            return NULL;

        lua_pushvalue(L, -1);

        if (!CheckAndDisableIfError(L, lua_pcall(L, 0, 1, 0)))
            return NULL;

        if (!lua_isnumber(L, -1)) {
            note::ToFile("[LuaJIT] %s", "The value returned from " GET_POSITION_ADDRESS " wasn't a number.");
            scriptingDisabled = true;
            return NULL;
        }

        auto result = (DWORD)lua_tointeger(L, -1);
        lua_pop(L, 1);
        return result;
    }

    void Uninitialize() {
        if (L != NULL)
            lua_close(L);
    }
}
