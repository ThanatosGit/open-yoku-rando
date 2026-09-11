#pragma once

#include <cstdint>

namespace oyr {

// Return values of open_yoku_rando.dll's `init` export, the exit code of the thread the xinput proxy starts it on.
// The proxy ignores them.
inline constexpr uint32_t kInitOk = 0x4F595231;  // "OYR1"
inline constexpr uint32_t kInitUnsupportedGame = 2;
inline constexpr uint32_t kInitHookFailed = 3;

}  // namespace oyr
