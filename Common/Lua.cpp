#include "framework.h"
#include <string>
#include <luajit/lua.hpp>
#include <fstream>
#include <sstream>

#include "Lua.h"
#include "LuaApi.h"
#include "Variables.h"
#include "macro.h"
#include "CallbackStore.h"
#include "MinHook.h"
#include "Log.h"
#include "Helper.Encoding.h"

namespace minhook = common::minhook;
namespace callbackstore = common::callbackstore;
namespace note = common::log;
namespace encoding = common::helper::encoding;
namespace luaapi = common::luaapi;

using namespace std;

#define ImportFunc(lua, luaDllName, funcName) \
(decltype(&funcName))GetProcAddress(lua, #funcName); \
if (!_ ## funcName) { \
    note::ToFile("[Lua] Failed to import %s|" #funcName ".", luaDllName.c_str()); \
    return; \
}0

namespace common::lua {
    int luaL_callmeta_hook(lua_State *L, int obj, const char *e);
    decltype(&luaL_callmeta_hook) ori_luaL_callmeta;
    void lua_call_hook(lua_State *L, int nargs, int nresults);
    decltype(&lua_call_hook) ori_lua_call;
    int lua_cpcall_hook(lua_State *L, lua_CFunction func, void *ud);
    decltype(&lua_cpcall_hook) ori_lua_cpcall;
    int lua_pcall_hook(lua_State *L, int nargs, int nresults, int errfunc);
    decltype(&lua_pcall_hook) ori_lua_pcall;

    decltype(&luaL_callmeta) _luaL_callmeta;
    decltype(&lua_call) _lua_call;
    decltype(&lua_cpcall) _lua_cpcall;
    decltype(&lua_pcall) _lua_pcall;

    decltype(&luaL_loadstring) _luaL_loadstring;
    decltype(&lua_tolstring) _lua_tolstring;
    decltype(&lua_settop) _lua_settop;

    bool Validate(lua_State* L, int r) {
        if (r != 0) {
            note::ToFile("[Lua] %s", _lua_tolstring(L, -1, 0));
            _lua_settop(L, -2);
            return false;
        }
        return true;
    }

    void AttachScript(lua_State *L);

    string luaDllName;
    string scriptPath;

    void Initialize() {
        if (g_currentConfig.ScriptType != ScriptType::Lua)
            return;
        // Only support Attached
        if (g_currentConfig.ScriptRunPlace != ScriptRunPlace::Attached)
            return;
        // Only support Push
        if (g_currentConfig.ScriptPositionGetMethod != ScriptPositionGetMethod::Push)
            return;

        {
            auto wScriptPath = wstring(g_currentModuleDirPath) + L"/ConfigScripts/" + g_currentConfig.ProcessName + L".lua";
            scriptPath = encoding::ConvertToUtf8(wScriptPath.c_str());
            ifstream scriptFile(scriptPath.c_str());
            if (!scriptFile) {
                note::ToFile("[Lua] Cannot open %s: %s.", scriptPath.c_str(), strerror(errno));
                return;
            }
            string firstLine;
            if (!getline(scriptFile, firstLine)) {
                note::ToFile("[Lua] Cannot read the first line of %s: %s.", scriptPath.c_str(), strerror(errno));
                return;
            }
            stringstream lineStream(firstLine);
            string token;
            lineStream >> token;
            if (token != "--") {
                note::ToFile("[Lua] The first line of '%s' is not a Lua comment.", scriptPath.c_str());
                return;
            }
            lineStream >> token;
            if (token != "LuaDllName") {
                note::ToFile("[Lua] The first Lua comment of '%s' doesn't have the key LuaDllName.", scriptPath.c_str());
                return;
            }
            lineStream >> token;
            if (token != "=") {
                note::ToFile("[Lua] Expected '=' after LuaDllName in '%s'.", scriptPath.c_str());
                return;
            }
            token = "";
            lineStream >> token;
            if (token == "") {
                note::ToFile("[Lua] LuaDllName value must be specified in '%s'.", scriptPath.c_str());
                return;
            }
            luaDllName = token;
        }

        auto luaPath = g_currentProcessDirPath + wstring(L"\\") + encoding::ConvertToUtf16(luaDllName.c_str());
        auto lua = GetModuleHandleW(luaPath.c_str());
        if (!lua) {
            note::ToFile("[Lua] Failed to load %s from the game's directory.", luaDllName.c_str());
            return;
        }

        _luaL_callmeta = ImportFunc(lua, luaDllName, luaL_callmeta);
        _lua_call = ImportFunc(lua, luaDllName, lua_call);
        _lua_cpcall = ImportFunc(lua, luaDllName, lua_cpcall);
        _lua_pcall = ImportFunc(lua, luaDllName, lua_pcall);

        _luaL_loadstring = ImportFunc(lua, luaDllName, luaL_loadstring);
        _lua_tolstring = ImportFunc(lua, luaDllName, lua_tolstring);
        _lua_settop = ImportFunc(lua, luaDllName, lua_settop);

        minhook::CreateHook(vector<minhook::HookConfig>{
            {_luaL_callmeta, &luaL_callmeta_hook, (PVOID*)&ori_luaL_callmeta},
            {_lua_call, &lua_call_hook, (PVOID*)&ori_lua_call},
            {_lua_cpcall, &lua_cpcall_hook, (PVOID*)&ori_lua_cpcall},
            {_lua_pcall, &lua_pcall_hook, (PVOID*)&ori_lua_pcall},
        });
    }

    int luaL_callmeta_hook(lua_State *L, int obj, const char *e) {
        auto rs = ori_luaL_callmeta(L, obj, e);
        AttachScript(L);
        minhook::RemoveHooks(vector<minhook::HookConfig> { { _luaL_callmeta, NULL, (PVOID*)ori_luaL_callmeta } });
        return rs;
    }
    void lua_call_hook(lua_State *L, int nargs, int nresults) {
        ori_lua_call(L, nargs, nresults);
        AttachScript(L);
        minhook::RemoveHooks(vector<minhook::HookConfig> { { _lua_call, NULL, (PVOID*)ori_lua_call } });
        return;
    }
    int lua_cpcall_hook(lua_State *L, lua_CFunction func, void *ud) {
        auto rs = ori_lua_cpcall(L, func, ud);
        AttachScript(L);
        minhook::RemoveHooks(vector<minhook::HookConfig> { { _lua_cpcall, NULL, (PVOID*)ori_lua_cpcall } });
        return rs;
    }
    int lua_pcall_hook(lua_State *L, int nargs, int nresults, int errfunc) {
        auto rs = ori_lua_pcall(L, nargs, nresults, errfunc);
        AttachScript(L);
        minhook::RemoveHooks(vector<minhook::HookConfig> { { _lua_pcall, NULL, (PVOID*)ori_lua_pcall } });
        return rs;
    }

    void AttachScript(lua_State *L) {
        static bool scriptAttached = false;
        if (scriptAttached)
            return;
        scriptAttached = true;

        auto rs = 0;
        if ((rs = _luaL_loadstring(L, luaapi::MakePreparationScript().c_str())) == 0)
            rs = ori_lua_pcall(L, 0, LUA_MULTRET, 0);
        if (!Validate(L, rs))
            return;

        auto scriptIn = fopen(scriptPath.c_str(), "rb");
        if (scriptIn == NULL) {
            note::ToFile("[Lua] Cannot open %s: %s.", scriptPath.c_str(), strerror(errno));
            return;
        }
        fseek(scriptIn, 0, SEEK_END);
        auto scriptSize = ftell(scriptIn);
        auto scriptContent = new char[scriptSize + 1];
        rewind(scriptIn);
        fread(scriptContent, sizeof(*scriptContent), scriptSize + 1, scriptIn);
        scriptContent[scriptSize] = '\0';
        fclose(scriptIn);
        if ((rs = _luaL_loadstring(L, (const char*)scriptContent)) == 0)
            rs = ori_lua_pcall(L, 0, LUA_MULTRET, 0);
        if (!Validate(L, rs))
            return;
        delete[] scriptContent;
    }

    DWORD GetPositionAddress() {
        return Lua_GetPositionAddress();
    }
}
