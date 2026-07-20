// Delay-load hook: when the delay-loaded "node.exe" import is first resolved,
// return a handle to the host process image instead of searching for a file on
// disk, so the addon works whatever the host executable is named (node.exe
// renamed, Electron/NW.js). Canonical node-gyp / cmake-js hook. MSVC only.

#ifdef _MSC_VER

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <delayimp.h>
#include <string.h>

static HMODULE node_dll = NULL;
static HMODULE nw_dll   = NULL;

static FARPROC WINAPI load_exe_hook(unsigned int event, DelayLoadInfo *info) {
    if (event == dliNotePreGetProcAddress) {
        FARPROC ret = GetProcAddress(node_dll, info->dlp.szProcName);
        if (ret)
            return ret;
        return GetProcAddress(nw_dll, info->dlp.szProcName);
    }
    if (event == dliStartProcessing) {
        node_dll = GetModuleHandleA("node.dll");
        nw_dll   = GetModuleHandleA("nw.dll");
        return NULL;
    }
    if (event != dliNotePreLoadLibrary)
        return NULL;

    if (_stricmp(info->szDll, "node.exe") != 0)
        return NULL;

    // Fall back to the current process.
    if (!node_dll)
        node_dll = GetModuleHandleA(NULL);

    return (FARPROC)node_dll;
}

decltype(__pfnDliNotifyHook2) __pfnDliNotifyHook2 = load_exe_hook;

#endif
