#pragma once

#include <windows.h>

#include <format>
#include <string_view>
#include <utility>

namespace oyr::log {

// Opens open_yoku_rando.log next to the DLL.
void open(HMODULE module);
void write(std::string_view message);

template <class... Args>
void info(std::format_string<Args...> format, Args&&... args) {
    write(std::format(format, std::forward<Args>(args)...));
}

// Receives every message as well, e.g. to forward it to a connected client. Must not log itself.
using Sink = void (*)(std::string_view message);
void set_sink(Sink sink);

}  // namespace oyr::log
