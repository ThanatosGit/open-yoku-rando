#pragma once

// Starting a Randovania seed: on a new randomizer game the loader's roll is skipped and the placement comes from
// open-yoku-rando/seed.txt. See "Starting a randomizer game" in docs/exe-research.md.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace oyr::new_game {

// The text key the menu patch points the entry at.
inline constexpr const char* kMenuTextKey = "ui_randovania_start";
// The entry's command without a usable seed file; no command handler matches it, so picking it does nothing.
inline constexpr const char* kNoSeedCommand = "randovania_no_seed_";

struct MenuEntry {
    std::string text;
    bool startable = false;  // a valid seed file is there
};
// Reads the seed file once at install time; the patcher only runs while the game is closed.
MenuEntry prepare_menu();
const std::string& menu_text();
// open-yoku-rando/texts.strings ("key;value" lines): Nothing texts and location lines, added to the dialog table
// by the tick.
const std::vector<std::pair<std::string, std::string>>& texts();
// The hash of a seed started from this game folder, by seed number; for slot labels.
const std::string* hash_for_seed(uint32_t seed);

// Around the randomizer loader. before_loader returns whether a new Randovania game is being set up.
bool before_loader(void* randomizer_state);
void after_loader(void* randomizer_state, bool new_game);

}  // namespace oyr::new_game
