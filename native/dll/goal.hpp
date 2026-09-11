#pragma once

#include <cstddef>
#include <string_view>

// The seed's goal, kept as markers in the save's `quests_completed` so it is saved and reverted with the game.
// Lit beacons are "@open-yoku-rando:beacon:<n>", one entry per beacon; Nim's ceremony checks for the n-th one.
// Beating the final boss is "@open-yoku-rando:victory".
namespace oyr::goal {

// Called after the game's beacon_use: lit_one is true when it took the Wickerlings and lit the beacon.
void on_beacon_used(bool lit_one);
// Called by the game's unlockAchievement native; "end_1" is the ending after the final boss.
void on_achievement(std::string_view name);

size_t beacons_lit();
bool beaten();

}  // namespace oyr::goal
