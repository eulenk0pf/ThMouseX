module;

#include "macro.h"
#include "framework.h"

export module common.var;

import common.datatype;

export HMODULE g_commonModule;
export WCHAR g_currentModuleDirPath[MAX_PATH + 1];

// single game config
export DLLEXPORT GameConfig       g_currentConfig;

// global game state
export DLLEXPORT bool             g_hookApplied;
export DLLEXPORT HMODULE          g_targetModule;
export DLLEXPORT HMODULE          g_coreModule;
export DLLEXPORT HWND             g_hFocusWindow;
export DLLEXPORT bool             g_leftMousePressed;
export DLLEXPORT bool             g_midMousePressed;
export DLLEXPORT bool             g_inputEnabled;
export DLLEXPORT float            g_pixelRate = 1;
export DLLEXPORT FloatPoint       g_pixelOffset{1, 1};

// configuration from main exe
#pragma data_seg(".SHRCONF")
export DLLEXPORT GameConfigArray  gs_gameConfigArray{};
export DLLEXPORT BYTE             gs_bombButton = 0x58; // VK_X
export DLLEXPORT BYTE             gs_extraButton = 0x43; // VK_C
export DLLEXPORT WCHAR            gs_textureFilePath[MAX_PATH]{};
export DLLEXPORT DWORD            gs_textureBaseHeight = 480;
export DLLEXPORT DWORD            gs_toggleOsCursorButton = 0x4D; // VK_M

export DLLEXPORT DWORD            gs_d3d9_CreateDevice_RVA{};
export DLLEXPORT DWORD            gs_d3d9_Reset_RVA{};
export DLLEXPORT DWORD            gs_d3d9_Present_RVA{};

export DLLEXPORT DWORD            gs_d3d8_CreateDevice_RVA{};
export DLLEXPORT DWORD            gs_d3d8_Reset_RVA{};
export DLLEXPORT DWORD            gs_d3d8_Present_RVA{};

export DLLEXPORT DWORD            gs_dinput8_GetDeviceState_RVA{};
#pragma data_seg()
// make the above segment shared across processes
#pragma comment(linker, "/SECTION:.SHRCONF,RWS")