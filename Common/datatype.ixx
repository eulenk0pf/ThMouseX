module;

#include "framework.h"
#include "macro.h"

export module common.datatype;

export constexpr auto PROCESS_NAME_MAX_LEN = 64;
export constexpr auto ADDRESS_CHAIN_MAX_LEN = 8;
export constexpr auto GAME_CONFIG_MAX_LEN = 128;

export enum ModulateStage {
    WhiteInc, WhiteDec, BlackInc, BlackDec,
};

export struct UINTSIZE {
    UINT width;
    UINT height;
};

export struct RECTSIZE: RECT {
    UNBOUND inline LONG width() const {
        return right;
    }
    UNBOUND inline LONG height() const {
        return bottom;
    }
};

export enum PointDataType {
    Int_DataType, Float_DataType, Short_DataType,
};

export struct IntPoint {
    int X;
    int Y;
};

export struct ShortPoint {
    short X;
    short Y;
};

export struct FloatPoint {
    float X;
    float Y;
};

export union TypedPoint {
    IntPoint    IntData;
    FloatPoint  FloatData;
};

export struct AddressChain {
    int     Length;
    DWORD   Level[ADDRESS_CHAIN_MAX_LEN];
    UNBOUND inline DWORD value(HMODULE baseAddress) const {
        auto address = Level[0] + (DWORD)baseAddress;
        for (int i = 1; i < Length; i++) {
            address = *(DWORD*)address;
            if (address == NULL)
                break;
            address += Level[i];
        }
        return address;
    }
};

export struct GameConfig {
    WCHAR           ProcessName[PROCESS_NAME_MAX_LEN];
    AddressChain    Address;
    PointDataType   PosDataType;
    FloatPoint      BasePixelOffset;
    unsigned int    BaseHeight;
    FloatPoint      AspectRatio;
};

export struct GameConfigArray {
    int         Length;
    GameConfig  Configs[GAME_CONFIG_MAX_LEN];
};
