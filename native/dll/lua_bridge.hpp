#pragma once

namespace oyr::bridge {

// Called from the tick hook: on the game thread, gives queued items and runs one queued remote Lua request.
void on_tick();

using NativeFn = int (*)(void* L);
// The game's unlockAchievement native wrapped so goal::on_achievement sees every name first.
NativeFn wrap_unlock_achievement(NativeFn original);

}  // namespace oyr::bridge
