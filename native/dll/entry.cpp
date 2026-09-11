#include "game.hpp"
#include "hooks.hpp"
#include "init_codes.hpp"
#include "log.hpp"
#include "remote_api.hpp"

#include <windows.h>

#include <atomic>
#include <string>

namespace {

HMODULE g_module = nullptr;

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    // not much to do here
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

// Started by the xinput proxy dll on a thread of its own. The low 16 bits of param are the port (0 = default).
extern "C" __declspec(dllexport) DWORD WINAPI init(void* param) {
    static std::atomic<bool> started = false;
    if (started.exchange(true)) {
        return oyr::kInitOk;
    }
    // A copy loaded from another folder has its own `started`, so a per-process named mutex lets only the first one hook.
    std::wstring name = L"Local\\open_yoku_rando_" + std::to_wstring(GetCurrentProcessId());
    if (CreateMutexW(nullptr, FALSE, name.c_str()) && GetLastError() == ERROR_ALREADY_EXISTS) {
        return oyr::kInitOk;
    }

    oyr::log::open(g_module);
    oyr::log::info("open_yoku_rando {} starting", OYR_VERSION);

    if (std::string error = oyr::game::verify(); !error.empty()) {
        oyr::log::info("unsupported game: {}", error);
        return oyr::kInitUnsupportedGame;
    }
    if (std::string error = oyr::hooks::install(); !error.empty()) {
        oyr::log::info("installing hooks failed: {}", error);
        return oyr::kInitHookFailed;
    }
    oyr::log::info("hooks installed");

    auto port = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(param) & 0xffff);
    oyr::remote::start(port != 0 ? port : oyr::remote::kDefaultPort);
    return oyr::kInitOk;
}
