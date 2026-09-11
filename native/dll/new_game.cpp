#include "new_game.hpp"

#include "game.hpp"
#include "log.hpp"

#include <windows.h>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oyr::new_game {
namespace {

namespace fs = std::filesystem;

constexpr int32_t kGameModeRandomizer = 3;
constexpr int32_t kDifficultyHard = 2;  // the logic open-yoku-rando's seeds are generated for
constexpr int kSeedFormat = 1;

// Offsets into the randomizer state at globals+0x3550, as the loader uses them.
constexpr uintptr_t kStateSeed = 0x80;
constexpr uintptr_t kStateDifficulty = 0x84;
constexpr uintptr_t kStateEntries = 0x88;

struct Seed {
    std::string identifier;
    std::string uuid;   // Randovania's layout UUID; optional
    std::string hash;   // what Randovania shows as the seed hash; optional
    uint32_t seed = 0;  // the schema allows the full 32 bits; the game keeps it in an int32
    int32_t fruit = 0;
    std::vector<std::pair<std::string, int32_t>> starting_items;
    std::map<uint32_t, std::string> placement;  // location id -> game item id
};

// Read by before_loader and used by after_loader of the same call.
std::optional<Seed> g_seed;

fs::path seed_path() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return fs::path(exe).parent_path() / L"open-yoku-rando" / L"seed.txt";
}

template <class T>
bool parse_number(std::string_view text, T& value) {
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size();
}

std::vector<std::string_view> split_tabs(std::string_view line) {
    std::vector<std::string_view> fields;
    for (size_t start = 0;;) {
        size_t tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab - start));
        if (tab == std::string_view::npos) {
            return fields;
        }
        start = tab + 1;
    }
}

// The patcher's seed file: UTF-8, one tab-separated record per line, '#' starts a comment line.
//   format      1
//   identifier  <Randovania's configuration identifier>
//   uuid        <Randovania's layout UUID>  (optional, for multiworld)
//   hash        <the seed hash as Randovania shows it>  (optional, for the menu)
//   seed        <number shown as the save's seed>
//   fruit       <starting fruit>
//   start       <item>  <count>        (any number of lines)
//   place       <location id>  <item>  (one line per location)
// Returns an error message, empty on success.
std::string read_seed(const fs::path& path, Seed& seed) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::format("there is no seed file at {}", path.string());
    }
    int format = 0;
    std::string line;
    for (int number = 1; std::getline(file, line); ++number) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        std::vector<std::string_view> f = split_tabs(line);
        bool ok = false;
        if (f[0] == "format" && f.size() == 2) {
            ok = parse_number(f[1], format);
        } else if (f[0] == "identifier" && f.size() == 2) {
            seed.identifier = f[1];
            ok = !seed.identifier.empty();
        } else if (f[0] == "uuid" && f.size() == 2) {
            seed.uuid = f[1];
            ok = !seed.uuid.empty();
        } else if (f[0] == "hash" && f.size() == 2) {
            seed.hash = f[1];
            ok = !seed.hash.empty();
        } else if (f[0] == "seed" && f.size() == 2) {
            ok = parse_number(f[1], seed.seed);
        } else if (f[0] == "fruit" && f.size() == 2) {
            ok = parse_number(f[1], seed.fruit);
        } else if (f[0] == "start" && f.size() == 3) {
            int32_t count = 0;
            ok = !f[1].empty() && parse_number(f[2], count) && count > 0;
            seed.starting_items.emplace_back(std::string(f[1]), count);
        } else if (f[0] == "place" && f.size() == 3) {
            uint32_t location = 0;
            ok = parse_number(f[1], location) && !f[2].empty() && seed.placement.emplace(location, std::string(f[2])).second;
        }
        if (!ok) {
            return std::format("{} line {} is not understood: {}", path.string(), number, line);
        }
    }
    if (format != kSeedFormat) {
        return std::format("{} has format {}, the DLL reads format {}", path.string(), format, kSeedFormat);
    }
    if (seed.identifier.empty() || seed.placement.empty()) {
        return std::format("{} has no identifier or no placement", path.string());
    }
    return {};
}

std::string g_menu_text;

// Seed number -> hash of every seed started here, kept in seed-history.txt ("<seed>\t<hash>" lines) because a slot
// outlives its seed file.
std::map<uint32_t, std::string> g_hash_by_seed;

fs::path history_path() {
    return seed_path().parent_path() / L"seed-history.txt";
}

std::vector<std::pair<std::string, std::string>> g_texts;

void load_texts() {
    fs::path path = seed_path().parent_path() / L"texts.strings";
    std::ifstream file(path, std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // A UTF-8 byte order mark, as the game's own .strings files start with.
        if (line.size() >= 3 && static_cast<uint8_t>(line[0]) == 0xef && static_cast<uint8_t>(line[1]) == 0xbb && static_cast<uint8_t>(line[2]) == 0xbf) {
            line.erase(0, 3);
        }
        size_t separator = line.find(';');
        if (separator != std::string::npos && separator > 0) {
            g_texts.emplace_back(line.substr(0, separator), line.substr(separator + 1));
        }
    }
    log::info("{} texts in {}", g_texts.size(), path.string());
}

std::string shown_hash(const Seed& seed) {
    return seed.hash.empty() ? seed.identifier : seed.hash;
}

void load_history() {
    std::ifstream file(history_path(), std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::vector<std::string_view> f = split_tabs(line);
        uint32_t number = 0;
        if (f.size() == 2 && parse_number(f[0], number) && !f[1].empty()) {
            g_hash_by_seed[number] = f[1];
        }
    }
}

void remember_seed(uint32_t number, const std::string& hash) {
    auto [entry, added] = g_hash_by_seed.try_emplace(number, hash);
    if (!added && entry->second == hash) {
        return;
    }
    entry->second = hash;
    std::ofstream(history_path(), std::ios::binary | std::ios::app) << number << '\t' << hash << '\n';
}

}  // namespace

const std::string* hash_for_seed(uint32_t seed) {
    auto entry = g_hash_by_seed.find(seed);
    return entry != g_hash_by_seed.end() ? &entry->second : nullptr;
}

const std::vector<std::pair<std::string, std::string>>& texts() {
    return g_texts;
}

MenuEntry prepare_menu() {
    load_history();
    load_texts();
    fs::path path = seed_path();
    Seed seed;
    MenuEntry entry;
    if (!fs::exists(path)) {
        entry.text = "No Randovania seed found";
        log::info("menu: there is no seed file at {}", path.string());
    } else if (std::string error = read_seed(path, seed); !error.empty()) {
        entry.text = "Randovania seed file is invalid";
        log::info("menu: {}", error);
    } else {
        entry.text = std::format("Start Randovania Seed: {}", shown_hash(seed));
        entry.startable = true;
        remember_seed(seed.seed, shown_hash(seed));
        log::info("menu: seed {} is ready", seed.identifier);
    }
    g_menu_text = entry.text;
    return entry;
}

const std::string& menu_text() {
    return g_menu_text;
}

bool before_loader(void* randomizer_state) {
    g_seed.reset();
    if (game::field<int32_t>(game::globals(), game::globals_offset::game_mode) != kGameModeRandomizer) {
        return false;
    }
    auto& entries = game::field<std::vector<game::RandomizerEntry>>(randomizer_state, kStateEntries);
    if (!entries.empty()) {
        return false;  // a loaded save, or a level after the first: the loader does not roll either
    }

    Seed seed;
    if (std::string error = read_seed(seed_path(), seed); !error.empty()) {
        log::info("new randomizer game without a Randovania seed, the game rolls its own: {}", error);
        return false;
    }
    game::field<int32_t>(randomizer_state, kStateDifficulty) = kDifficultyHard;
    game::field<uint32_t>(randomizer_state, kStateSeed) = seed.seed;
    // The loader only rolls into an empty entry vector. With a placeholder it treats the game like a loaded save:
    // fills every entry from the CSV but keeps the items, which after_loader then sets.
    entries.emplace_back();
    g_seed = std::move(seed);
    return true;
}

void after_loader(void* randomizer_state, bool new_game) {
    if (!new_game || !g_seed) {
        return;
    }
    Seed seed = std::move(*g_seed);
    g_seed.reset();

    auto& entries = game::field<std::vector<game::RandomizerEntry>>(randomizer_state, kStateEntries);
    size_t placed = 0;
    for (game::RandomizerEntry& entry : entries) {
        auto item = seed.placement.find(static_cast<uint32_t>(entry.id));
        if (item == seed.placement.end()) {
            log::info("the seed places nothing at location {} ({}); it keeps an empty item", entry.id, entry.vanilla_item);
            continue;
        }
        entry.item = item->second;
        ++placed;
    }
    if (placed != seed.placement.size()) {
        log::info("the seed names {} locations the game does not have", seed.placement.size() - placed);
    }

    for (const auto& [item, count] : seed.starting_items) {
        for (int32_t i = 0; i < count; ++i) {
            game::inventory_add(item);
        }
    }
    if (seed.fruit > 0) {
        game::add_fruit(seed.fruit);
    }
    if (std::vector<std::string>* quests = game::quests_completed()) {
        quests->push_back(std::string(game::kIdentifierPrefix) + seed.identifier);
        if (!seed.uuid.empty()) {
            quests->push_back(std::string(game::kLayoutUuidPrefix) + seed.uuid);
        }
    }
    remember_seed(seed.seed, shown_hash(seed));
    log::info("started Randovania seed {} (seed {}) on thread {}: {} of {} locations placed, {} starting items, {} fruit", seed.identifier, seed.seed,
              GetCurrentThreadId(), placed, entries.size(), seed.starting_items.size(), seed.fruit);
}

}  // namespace oyr::new_game
