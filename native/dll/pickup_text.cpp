// Shows each location's line (texts.strings: "items/nothing_<location>_1") when its pickup is collected.

#include "game.hpp"
#include "log.hpp"

#include <deque>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace oyr::game {
namespace {

// Each Nothing pickup overwrites "items/nothing_1"; the patcher's default is kept for locations without a line.
std::optional<std::string> g_default_nothing_text;

// A real item's own "items/<item>_1", saved before it is first replaced (nullopt if it had none), so a location
// without a line gets the game's text back.
std::map<std::string, std::optional<std::string>> g_original_item_text;

// The dialog a real item's pickup opens instead of "items/<item>". A translation would override a changed
// "items/<item>_1", but none has "items/nothing_<location>_1".
std::string g_pickup_dialog_name;

// Pickups that skip pickup_dialog (Wickerlings) are found by their `collected` flag instead.
std::set<uint32_t> g_dialog_locations;  // already handled by the hook
std::vector<bool> g_seen_collected;
std::deque<std::pair<std::string, std::string>> g_location_dialogs;  // still to show

// The game keeps a Wickerling as "collectible", and that is where its texts live.
std::string text_id(const std::string& item) {
    return item == "wickerling" ? "collectible" : item;
}

std::string location_line_key(uint32_t location) {
    return std::format("items/{}_{}_1", nothing_item, location);
}

std::string item_title(const std::string& item) {
    for (const std::string& id : {item, text_id(item)}) {
        bool found = false;
        std::string name = get_string(std::format("items/{}_name", id), &found);
        if (found) {
            return name;
        }
    }
    return item;
}

void show_location_text_for_item(const std::string& item, uint32_t location) {
    bool found = false;
    std::string text = location != 0 ? get_string(location_line_key(location), &found) : std::string();
    for (const std::string& id : {item, item == "wickerling" ? std::string("collectible") : std::string()}) {
        if (id.empty()) {
            continue;
        }
        std::string shown = std::format("items/{}_1", id);
        auto original = g_original_item_text.find(id);
        if (original == g_original_item_text.end()) {
            bool had = false;
            std::string own = get_string(shown, &had);
            original = g_original_item_text.emplace(id, had ? std::optional(own) : std::nullopt).first;
        }
        if (found) {
            // Fruit and Wickerlings have no name of their own; give them one so the speaker isn't blank.
            std::string name = std::format("items/{}_name", id);
            bool named = false;
            get_string(name, &named);
            if (!named) {
                set_string(name, id);
            }
            set_string(shown, text);
            g_pickup_dialog_name = std::format("items/{}_{}", nothing_item, location);
            std::string speaker = g_pickup_dialog_name + "_name";
            bool has_speaker = false;
            get_string(speaker, &has_speaker);
            if (!has_speaker) {
                set_string(speaker, item_title(item));
            }
        } else if (original->second) {
            set_string(shown, *original->second);
        } else {
            erase_string(shown);
        }
    }
    log::info("{} pickup at location {}: {}", item, location, found ? "showing the location's line" : "own text");
}

}  // namespace

bool is_nothing_item(const std::string& item) {
    return item == nothing_item || item.starts_with(std::string(nothing_item) + "_");
}

const char* pickup_dialog_name(const char* game_name) {
    return g_pickup_dialog_name.empty() ? game_name : g_pickup_dialog_name.c_str();
}

void end_bubble_pickup() {
    g_pickup_dialog_name.clear();
}

void on_bubble_pickup(void* pickup) {
    auto& item = field<std::string>(pickup, pickup_offset::item);
    uint32_t location = pickup_location(pickup);
    g_pickup_dialog_name.clear();
    g_dialog_locations.insert(location);
    if (!is_nothing_item(item)) {
        show_location_text_for_item(item, location);
        return;
    }
    std::string shown = std::format("items/{}_1", nothing_item);
    if (!g_default_nothing_text) {
        g_default_nothing_text = get_string(shown);
    }

    bool found = false;
    std::string key = location_line_key(location);
    std::string text = location != 0 ? get_string(key, &found) : std::string();
    set_string(shown, found ? text : *g_default_nothing_text);

    // Before the game's inventory_add, so every Nothing stacks in one inventory slot.
    if (item != nothing_item) {
        log::info("Nothing pickup: collapsing {} to {}", item, nothing_item);
        item = nothing_item;
    }
    log::info("Nothing pickup at location {}: {} {}", location, found ? "showing" : "no text baked for", found ? text : key);
}

void watch_collected_locations() {
    std::span<RandomizerEntry> entries = randomizer_entries();
    if (!in_game() || entries.empty()) {
        g_seen_collected.clear();
        g_dialog_locations.clear();
        g_location_dialogs.clear();
        return;
    }
    // The first look at a loaded save only takes note of what was collected before.
    if (g_seen_collected.size() != entries.size()) {
        g_seen_collected.assign(entries.size(), false);
        for (size_t i = 0; i < entries.size(); ++i) {
            g_seen_collected[i] = entries[i].collected;
        }
        g_dialog_locations.clear();
        return;
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        const RandomizerEntry& entry = entries[i];
        if (!entry.collected || g_seen_collected[i]) {
            continue;
        }
        g_seen_collected[i] = true;
        auto location = static_cast<uint32_t>(entry.id);
        if (g_dialog_locations.erase(location) > 0) {
            continue;
        }
        bool found = false;
        std::string text = get_string(location_line_key(location), &found);
        log::info("{} collected at location {} without pickup_dialog: {}", entry.item, location, found ? "showing the location's line" : "no line baked");
        if (found) {
            g_location_dialogs.emplace_back(item_title(entry.item), text);
        }
    }
}

std::optional<std::pair<std::string, std::string>> pop_location_dialog() {
    if (g_location_dialogs.empty()) {
        return std::nullopt;
    }
    auto dialog = std::move(g_location_dialogs.front());
    g_location_dialogs.pop_front();
    return dialog;
}

}  // namespace oyr::game
