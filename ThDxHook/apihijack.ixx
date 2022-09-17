/*--------------------------------------------------------------------------------------------------------
    APIHIJACK.CPP - Based on DelayLoadProfileDLL.CPP, by Matt Pietrek for MSJ February 2000.
    http://msdn.microsoft.com/library/periodic/period00/hood0200.htm
    Adapted by Wade Brainerd, wadeb@wadeb.com
    Adapted by Martin Mueller, wingcommander@usa.net
--------------------------------------------------------------------------------------------------------*/
module;

#include "framework.h"

export module core.apihijack;

#pragma region Interface
struct SDLLHook;
// Hook functions one or more DLLs.
export bool HookAPICalls(SDLLHook* Hook);

export struct SFunctionHook {
    const char* Name;        // Function name, e.g. "DirectInput8Create".
    DWORD*      HookFn;      // Address of your function.
    DWORD*      OrigFn;      // Stored by HookAPICalls, the address of the original function.
};

#pragma warning(disable: 4200) // Trailing Array Idiom
export struct SDLLHook {
    // Name of the DLL, e.g. "DDRAW.DLL"
    const char* Name;

    // Set true to call the default for all non-hooked functions before they are executed.
    bool  UseDefault;
    void* DefaultFn;

    // Function hook array. Terminated with a NULL Name field.
    SFunctionHook Functions[];
};
#pragma endregion

#pragma region Implementation
// Macro for convenient pointer addition.
// Essentially treats the last two parameters as DWORDs.  The first
// parameter is used to typecast the result to the appropriate pointer type.
#define MakePtr(cast, ptr, addValue) (cast)((DWORD)(ptr)+(DWORD)(addValue))

// Default Hook Stub Structure: Contains data about the original function, Name/Ordinal, Address 
// and a Count field. This is actually a block of assembly code.
#pragma pack(push, 1)
struct DLPD_IAT_STUB {
    BYTE    instr_CALL;
    DWORD   data_call;
    BYTE    instr_JMP;
    DWORD   data_JMP;
    DWORD   count;
    DWORD   pszNameOrOrdinal;

    DLPD_IAT_STUB(): instr_CALL(0xE8), instr_JMP(0xE9), count(0) {}
};
#pragma pack(pop)

//===========================================================================
// Called from the DLPD_IAT_STUB stubs.  Increments "count" field of the stub
void __cdecl DefaultHook(PVOID dummy) {
    __asm   pushad  // Save all general purpose registers

    // Get return address, then subtract 5 (size of a CALL X instruction)
    // The result points at a DLPD_IAT_STUB

    // pointer math!  &dummy-1 really subtracts sizeof(PVOID)
    PDWORD pRetAddr = (PDWORD)(&dummy - 1);

    DLPD_IAT_STUB* pDLPDStub = (DLPD_IAT_STUB*)(*pRetAddr - 5);

    pDLPDStub->count++;

    __asm   popad   // Restore all general purpose registers
}

// This function must be __cdecl!!!
void __cdecl DelayLoadProfileDLL_UpdateCount(PVOID dummy);

PIMAGE_IMPORT_DESCRIPTOR g_pFirstImportDesc;

//===========================================================================
// Given an HMODULE, returns a pointer to the PE header
PIMAGE_NT_HEADERS PEHeaderFromHModule(HMODULE hModule) {
    PIMAGE_NT_HEADERS pNTHeader = 0;

    __try {
        if (PIMAGE_DOS_HEADER(hModule)->e_magic != IMAGE_DOS_SIGNATURE)
            __leave;

        pNTHeader = PIMAGE_NT_HEADERS(PBYTE(hModule) + PIMAGE_DOS_HEADER(hModule)->e_lfanew);

        if (pNTHeader->Signature != IMAGE_NT_SIGNATURE)
            pNTHeader = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { }

    return pNTHeader;
}

//===========================================================================
// Builds stubs for and redirects the IAT for one DLL (pImportDesc)
bool RedirectIAT(SDLLHook* DLLHook, PIMAGE_IMPORT_DESCRIPTOR pImportDesc, PVOID pBaseLoadAddr) {
    PIMAGE_THUNK_DATA pIAT;     // Ptr to import address table
    PIMAGE_THUNK_DATA pINT;     // Ptr to import names table
    PIMAGE_THUNK_DATA pIteratingIAT;

    // Figure out which OS platform we're on
    OSVERSIONINFO osvi{};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    GetVersionEx(&osvi);

    // If no import names table, we can't redirect this, so bail
    if (pImportDesc->OriginalFirstThunk == 0)
        return false;

    pIAT = MakePtr(PIMAGE_THUNK_DATA, pBaseLoadAddr, pImportDesc->FirstThunk);
    pINT = MakePtr(PIMAGE_THUNK_DATA, pBaseLoadAddr, pImportDesc->OriginalFirstThunk);

    // Count how many entries there are in this IAT.  Array is 0 terminated
    pIteratingIAT = pIAT;
    unsigned cFuncs = 0;
    while (pIteratingIAT->u1.Function) {
        cFuncs++;
        pIteratingIAT++;
    }

    if (cFuncs == 0)  // If no imported functions, we're done!
        return false;

    // These next few lines ensure that we'll be able to modify the IAT,
    // which is often in a read-only section in the EXE.
    DWORD flOldProtect, flNewProtect, flDontCare;
    MEMORY_BASIC_INFORMATION mbi;

    // Get the current protection attributes                            
    VirtualQuery(pIAT, &mbi, sizeof(mbi));

    // remove ReadOnly and ExecuteRead and Execute attributes, add on ReadWrite flag
    flNewProtect = mbi.Protect;
    flNewProtect &= ~(PAGE_READONLY | PAGE_EXECUTE_READ | PAGE_EXECUTE);
    flNewProtect |= (PAGE_READWRITE);

    if (!VirtualProtect(pIAT, sizeof(PVOID) * cFuncs,
        flNewProtect, &flOldProtect)) {
        return false;
    }

    // If the Default hook is enabled, build an array of redirection stubs in the processes memory.
    DLPD_IAT_STUB* pStubs = 0;
    if (DLLHook->UseDefault) {
        // Allocate memory for the redirection stubs.  Make one extra stub at the
        // end to be a sentinel
        pStubs = new DLPD_IAT_STUB[cFuncs + 1];
        if (!pStubs)
            return false;
    }

    // Scan through the IAT, completing the stubs and redirecting the IAT
    // entries to point to the stubs
    pIteratingIAT = pIAT;

    while (pIteratingIAT->u1.Function) {
        void* HookFn = 0;  // Set to either the SFunctionHook or pStubs.

        if (!IMAGE_SNAP_BY_ORDINAL(pINT->u1.Ordinal))  // import by name
        {
            PIMAGE_IMPORT_BY_NAME pImportName = MakePtr(PIMAGE_IMPORT_BY_NAME, pBaseLoadAddr, pINT->u1.AddressOfData);

            // Iterate through the hook functions, searching for this import.
            SFunctionHook* FHook = DLLHook->Functions;
            while (FHook->Name) {
                if (lstrcmpi(FHook->Name, (char*)pImportName->Name) == 0) {
                    // Save the old function in the SFunctionHook structure and get the new one.
                    FHook->OrigFn = (unsigned long*)pIteratingIAT->u1.Function;
                    HookFn = FHook->HookFn;
                    break;
                }

                FHook++;
            }

            // If the default function is enabled, store the name for the user.
            if (DLLHook->UseDefault)
                pStubs->pszNameOrOrdinal = (DWORD)&pImportName->Name;
        } else {
            // If the default function is enabled, store the ordinal for the user.
            if (DLLHook->UseDefault)
                pStubs->pszNameOrOrdinal = pINT->u1.Ordinal;
        }

        // If the default function is enabled, fill in the fields to the stub code.
        if (DLLHook->UseDefault) {
            pStubs->data_call = (DWORD)(PDWORD)DLLHook->DefaultFn
                - (DWORD)(PDWORD)&pStubs->instr_JMP;
            pStubs->data_JMP = *(PDWORD)pIteratingIAT - (DWORD)(PDWORD)&pStubs->count;

            // If it wasn't manually hooked, use the Stub function.
            if (!HookFn)
                HookFn = (void*)pStubs;
        }

        // Replace the IAT function pointer if we have a hook.
        if (HookFn) {
            // Cheez-o hack to see if what we're importing is code or data.
            // If it's code, we shouldn't be able to write to it
            if (IsBadWritePtr((PVOID)pIteratingIAT->u1.Function, 1)) {
                pIteratingIAT->u1.Function = (DWORD)HookFn;
            } else if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
                // Special hack for Win9X, which builds stubs for imported
                // functions in system DLLs (Loaded above 2GB).  These stubs are
                // writeable, so we have to explicitly check for this case
                if (pIteratingIAT->u1.Function > (DWORD)0x80000000)
                    pIteratingIAT->u1.Function = (DWORD)HookFn;
            }
        }

        if (DLLHook->UseDefault)
            pStubs++;           // Advance to next stub

        pIteratingIAT++;    // Advance to next IAT entry
        pINT++;             // Advance to next INT entry
    }

    if (DLLHook->UseDefault)
        pStubs->pszNameOrOrdinal = 0;   // Final stub is a sentinel

    // Put the page attributes back the way they were.
    VirtualProtect(pIAT, sizeof(PVOID) * cFuncs, flOldProtect, &flDontCare);

    return true;
}

//===========================================================================
// Top level routine to find the EXE's imports, and redirect them
bool HookAPICalls(SDLLHook* Hook) {
    if (!Hook)
        return false;

    HMODULE hModEXE = GetModuleHandle(0);

    PIMAGE_NT_HEADERS pExeNTHdr = PEHeaderFromHModule(hModEXE);

    if (!pExeNTHdr)
        return false;

    DWORD importRVA = pExeNTHdr->OptionalHeader.DataDirectory
        [IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRVA)
        return false;

    // Convert imports RVA to a usable pointer
    PIMAGE_IMPORT_DESCRIPTOR pImportDesc = MakePtr(PIMAGE_IMPORT_DESCRIPTOR,
        hModEXE, importRVA);

    // Save off imports address in a global for later use
    g_pFirstImportDesc = pImportDesc;

    // Iterate through each import descriptor, and redirect if appropriate
    while (pImportDesc->FirstThunk) {
        PSTR pszImportModuleName = MakePtr(PSTR, hModEXE, pImportDesc->Name);

        if (lstrcmpi(pszImportModuleName, Hook->Name) == 0) {
            RedirectIAT(Hook, pImportDesc, (PVOID)hModEXE);
        }

        pImportDesc++;  // Advance to next import descriptor
    }

    return true;
}
#pragma endregion