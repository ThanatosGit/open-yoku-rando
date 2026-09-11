#include "log.hpp"

#include <share.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

namespace oyr::log {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
std::atomic<Sink> g_sink = nullptr;

}  // namespace

void open(HMODULE module) {
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(module, buffer, MAX_PATH);
    std::wstring path(buffer, length);
    path = path.substr(0, path.find_last_of(L"\\/") + 1) + L"open_yoku_rando.log";

    std::lock_guard lock(g_mutex);
    if (!g_file) {
        g_file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
    }
}

void write(std::string_view message) {
    SYSTEMTIME time;
    GetLocalTime(&time);
    std::string line =
        std::format("{:02}:{:02}:{:02}.{:03} [{}] {}\n", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentThreadId(), message);
    {
        std::lock_guard lock(g_mutex);
        if (g_file) {
            std::fwrite(line.data(), 1, line.size(), g_file);
            std::fflush(g_file);
        }
    }
    if (Sink sink = g_sink.load()) {
        sink(message);
    }
}

void set_sink(Sink sink) {
    g_sink = sink;
}

}  // namespace oyr::log
