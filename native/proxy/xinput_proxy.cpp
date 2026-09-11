// Proxy-DLL for xinput9_1_0.dll. Forwards XInputGetState to the system DLL and starts open-yoku-rando\open_yoku_rando.dll

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

HMODULE g_self = nullptr;

using XInputGetStateFn = DWORD(WINAPI*)(DWORD user_index, void* state);
using InitFn = DWORD(WINAPI*)(void* param);

// gets the folder of where this module is located
std::wstring folder_of(HMODULE module) {
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    std::wstring folder(path, length);
    return folder.substr(0, folder.find_last_of(L"\\/") + 1);
}

// A thread created in DllMain waits for the loader lock, so LoadLibrary is safe here.
DWORD WINAPI start_dll(void*) {
    // load open_yoku_rando.dll,find the init method and call it with port (= 0 uses the default port)
    HMODULE dll = LoadLibraryW((folder_of(g_self) + L"open-yoku-rando\\open_yoku_rando.dll").c_str());
    if (!dll) {
        OutputDebugStringW(L"xinput proxy: open-yoku-rando\\open_yoku_rando.dll not found\n");
        return 1;
    }
    auto init = reinterpret_cast<InitFn>(reinterpret_cast<void*>(GetProcAddress(dll, "init")));
    if (!init) {
        return 1;
    }
    uintptr_t port = 0;
    wchar_t value[16];
    if (GetEnvironmentVariableW(L"OPEN_YOKU_RANDO_PORT", value, 16) > 0) {
        port = std::wcstoul(value, nullptr, 10) & 0xffff;
    }
    return init(reinterpret_cast<void*>(port));
}

// Epic's overlay hooks GetProcAddress and would hand us its own XInputGetState, which calls back into this proxy.
// So the function is looked up by hand in the system dll's export table.
void* find_export(HMODULE module, const char* name) {
    auto* base = reinterpret_cast<uint8_t*>(module);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    auto* exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
    auto* names = reinterpret_cast<DWORD*>(base + exports->AddressOfNames);
    auto* ordinals = reinterpret_cast<WORD*>(base + exports->AddressOfNameOrdinals);
    auto* functions = reinterpret_cast<DWORD*>(base + exports->AddressOfFunctions);
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        if (std::strcmp(reinterpret_cast<const char*>(base + names[i]), name) != 0) {
            continue;
        }
        DWORD rva = functions[ordinals[i]];
        bool forwarded = rva >= directory.VirtualAddress && rva < directory.VirtualAddress + directory.Size;
        return forwarded ? nullptr : base + rva;
    }
    return nullptr;
}

// find xinput9_1_0.dll in system directory
XInputGetStateFn system_xinput_get_state() {
    static XInputGetStateFn function = [] {
        wchar_t system[MAX_PATH];
        UINT length = GetSystemDirectoryW(system, MAX_PATH);
        std::wstring path = std::wstring(system, length) + L"\\xinput9_1_0.dll";
        HMODULE module = LoadLibraryW(path.c_str());
        return module ? reinterpret_cast<XInputGetStateFn>(find_export(module, "XInputGetState")) : nullptr;
    }();
    return function;
}

}  // namespace

// XInputGetState is the only function called from the game which needs to be proxied
extern "C" __declspec(dllexport) DWORD WINAPI XInputGetState(DWORD user_index, void* state) {
    XInputGetStateFn function = system_xinput_get_state();
    return function ? function(user_index, state) : ERROR_DEVICE_NOT_CONNECTED;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    // when the dll is attached, execute start_dll in a new thread and return
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        if (HANDLE thread = CreateThread(nullptr, 0, start_dll, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
