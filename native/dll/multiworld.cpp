#include "multiworld.hpp"

#include "game.hpp"
#include "goal.hpp"
#include "log.hpp"
#include "remote_api.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <optional>
#include <unordered_map>
#include <vector>

namespace oyr::multiworld {
namespace {

// The received count is kept in the save's quests_completed, so a reload cannot deliver twice.
constexpr std::string_view kReceivedPrefix = "@open-yoku-rando:received:";

// Twice a second at the game's 60 Hz, which is what Randovania's other games poll at.
constexpr uint32_t kTicksBetweenPolls = 30;

// Set once during the handshake, then only read.
std::vector<uint32_t> g_locations;
std::unordered_map<uint32_t, size_t> g_location_index;
std::vector<std::string> g_items;
std::string g_identifier_marker;

// What the client was last told, so only changes are sent. Cleared on connect and in the main menu, where
// Randovania forgets its state too.
uint64_t g_connection = 0;
std::optional<std::string> g_sent_state;
std::optional<std::string> g_sent_locations;
std::optional<std::string> g_sent_inventory;
std::optional<std::string> g_sent_received;
uint32_t g_ticks = 0;
bool g_reported_unknown_locations = false;
bool g_reported_other_save = false;

// Counts changes of the reported item counts, so Randovania can tell whether the inventory it named a remote pickup
// from is still current. Kept in memory only: loading another save changes the counts, which counts as a change.
size_t g_inventory_index = 0;
std::vector<int32_t> g_inventory_snapshot;

size_t parse_list(std::string_view text, auto&& add) {
    size_t count = 0;
    for (size_t start = 0; start <= text.size();) {
        size_t comma = text.find(',', start);
        std::string_view field = text.substr(start, comma == std::string_view::npos ? comma : comma - start);
        if (!field.empty() && !add(field)) {
            return 0;
        }
        count += field.empty() ? 0 : 1;
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return count;
}

// Randovania drops these on MAINMENU, so they are resent once a save is loaded.
void forget_the_save_state() {
    g_sent_locations.reset();
    g_sent_inventory.reset();
    g_sent_received.reset();
}

void forget_what_was_sent() {
    g_sent_state.reset();
    forget_the_save_state();
}

void send_if_new(remote::Signal signal, std::optional<std::string>& sent, std::string payload) {
    if (sent == payload) {
        return;
    }
    remote::send_signal(signal, payload);
    sent = std::move(payload);
}

// "<region>;<beaten>;<level>". The region is the level-name prefix, matched against region.extra["scenario_id"].
std::string game_state_payload() {
    if (!game::in_game()) {
        return "MAINMENU;false";
    }
    std::string level = game::current_level();
    std::string_view region = std::string_view(level).substr(0, level.find('_'));
    return std::format("{};{};{}", region, goal::beaten() ? "true" : "false", level);
}

// "locations:" and one bit per location in Randovania's order, LSB first. Always the full set.
std::string collected_locations_payload() {
    std::string payload = "locations:";
    payload.append((g_locations.size() + 7) / 8, '\0');
    char* bits = payload.data() + payload.size() - (g_locations.size() + 7) / 8;

    size_t known = 0;
    for (const game::RandomizerEntry& entry : game::randomizer_entries()) {
        auto found = g_location_index.find(static_cast<uint32_t>(entry.id));
        if (found == g_location_index.end()) {
            continue;
        }
        ++known;
        if (entry.collected) {
            bits[found->second / 8] |= static_cast<char>(1 << (found->second % 8));
        }
    }
    if (known != g_locations.size() && !g_reported_unknown_locations) {
        g_reported_unknown_locations = true;
        log::info("the save holds {} of the {} locations Randovania knows", known, g_locations.size());
    }
    return payload;
}

std::string inventory_payload() {
    std::string counts;
    for (int32_t count : g_inventory_snapshot) {
        counts += counts.empty() ? "" : ",";
        counts += std::to_string(count);
    }
    return std::format("{{\"index\": {},\"inventory\":[{}]}}", g_inventory_index, counts);
}


}  // namespace

size_t set_locations(std::string_view ids) {
    std::vector<uint32_t> locations;
    size_t count = parse_list(ids, [&locations](std::string_view field) {
        uint32_t id = 0;
        auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), id);
        if (error != std::errc{} || end != field.data() + field.size()) {
            return false;
        }
        locations.push_back(id);
        return true;
    });
    if (count == 0) {
        log::info("could not parse the location list");
        return 0;
    }

    g_locations = std::move(locations);
    g_location_index.clear();
    for (size_t index = 0; index < g_locations.size(); ++index) {
        g_location_index.emplace(g_locations[index], index);
    }
    if (g_location_index.size() != g_locations.size()) {
        log::info("the location list has duplicate ids; {} of {} are distinct", g_location_index.size(), g_locations.size());
    }
    forget_what_was_sent();
    g_reported_unknown_locations = false;
    log::info("multiworld armed with {} locations", g_locations.size());
    return g_locations.size();
}

size_t set_inventory_items(std::string_view items) {
    std::vector<std::string> parsed;
    size_t count = parse_list(items, [&parsed](std::string_view field) {
        parsed.emplace_back(field);
        return true;
    });
    g_items = std::move(parsed);
    g_inventory_snapshot.clear();
    g_sent_inventory.reset();
    return count;
}

void set_identifier(std::string_view identifier) {
    g_identifier_marker = identifier.empty() ? std::string() : std::string(game::kIdentifierPrefix) + std::string(identifier);
    g_reported_other_save = false;
    forget_what_was_sent();
}

bool armed() {
    return !g_locations.empty();
}

void update_inventory_index() {
    if (g_items.empty() || !game::in_game()) {
        return;
    }
    std::vector<int32_t> counts;
    counts.reserve(g_items.size());
    for (const std::string& item : g_items) {
        counts.push_back(game::item_count(item));
    }
    if (counts != g_inventory_snapshot) {
        g_inventory_snapshot = std::move(counts);
        ++g_inventory_index;
    }
}

size_t inventory_index() {
    return g_inventory_index;
}

void resend_save_state() {
    forget_the_save_state();
}

bool save_matches() {
    if (g_identifier_marker.empty()) {
        return true;
    }
    const std::vector<std::string>* quests = game::quests_completed();
    bool matches = quests && std::ranges::find(*quests, g_identifier_marker) != quests->end();
    if (!matches && !g_reported_other_save) {
        g_reported_other_save = true;
        log::info("the loaded save is not {}; reporting nothing for it", g_identifier_marker);
    }
    return matches;
}

std::string layout_uuid() {
    const std::vector<std::string>* quests = game::quests_completed();
    if (!quests) {
        return {};
    }
    for (const std::string& quest : *quests) {
        if (quest.starts_with(game::kLayoutUuidPrefix)) {
            return quest.substr(game::kLayoutUuidPrefix.size());
        }
    }
    return {};
}

size_t received_count() {
    const std::vector<std::string>* quests = game::quests_completed();
    if (!quests) {
        return 0;
    }
    for (const std::string& quest : *quests) {
        if (!quest.starts_with(kReceivedPrefix)) {
            continue;
        }
        size_t value = 0;
        std::string_view digits = std::string_view(quest).substr(kReceivedPrefix.size());
        if (std::from_chars(digits.data(), digits.data() + digits.size(), value).ec == std::errc{}) {
            return value;
        }
    }
    return 0;
}

void note_pickup_received() {
    if (!save_matches()) {
        return;
    }
    std::vector<std::string>* quests = game::quests_completed();
    if (!quests) {
        log::info("cannot record the received pickup: the game has no save loaded");
        return;
    }
    std::string marker = std::format("{}{}", kReceivedPrefix, received_count() + 1);
    for (std::string& quest : *quests) {
        if (quest.starts_with(kReceivedPrefix)) {
            quest = marker;
            return;
        }
    }
    quests->push_back(marker);
}

void on_tick() {
    if (!armed() || !remote::multiworld_client_connected()) {
        return;
    }
    // A client that reconnects starts from nothing and has to be told everything again.
    if (uint64_t connection = remote::connection_generation(); connection != g_connection) {
        g_connection = connection;
        forget_what_was_sent();
    }
    if (++g_ticks < kTicksBetweenPolls) {
        return;
    }
    g_ticks = 0;

    // State first: Randovania only accepts a received count once it knows the region.
    send_if_new(remote::Signal::kGameState, g_sent_state, game_state_payload());
    if (!game::in_game()) {
        forget_the_save_state();
        return;
    }
    // Wait for the save; never report a save that is not the generated one.
    if (game::randomizer_entries().empty() || !save_matches()) {
        return;
    }

    update_inventory_index();
    send_if_new(remote::Signal::kInventory, g_sent_inventory, inventory_payload());
    send_if_new(remote::Signal::kCollectedLocations, g_sent_locations, collected_locations_payload());
    send_if_new(remote::Signal::kReceivedPickups, g_sent_received, std::to_string(received_count()));
}

}  // namespace oyr::multiworld
