#pragma once

#include "builds.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oyr::game {

// Offsets into the object behind Build::globals_ptr; the same in all builds.
namespace globals_offset {
inline constexpr uintptr_t level = 0x25b8;      // std::string; "_start_screen" in the main menu
inline constexpr uintptr_t inventory = 0x29e8;  // std::vector<InventoryItem>
// std::vector<std::string>; the game keeps entries it does not know, which is how the patcher marks a save.
inline constexpr uintptr_t quests_completed = 0x2688;
inline constexpr uintptr_t randomizer_entries = 0x35d8;  // std::vector<RandomizerEntry>, in save order
inline constexpr uintptr_t frame_counter = 0x2028;       // uint32
inline constexpr uintptr_t fruit = 0x2a30;               // int32, the save's `fruit`
inline constexpr uintptr_t total_fruit = 0x2a34;         // int32, the save's `total_fruit`
inline constexpr uintptr_t game_mode = 0x93b0;           // int32; 3 is a randomizer game
inline constexpr uintptr_t randomizer_state = 0x3550;    // what the randomizer loader gets
inline constexpr uintptr_t dialog_flag = 0x4dd0;
inline constexpr uintptr_t string_table = 0x4de0;
inline constexpr uintptr_t ui_file = 0x9018;
inline constexpr uintptr_t dialog_file = 0x9028;
inline constexpr uintptr_t file_strings = 0x78;  // std::vector<std::array<std::string, 2>> in a text file object
inline constexpr uintptr_t world = 0x1f80;        // World*, the running game
inline constexpr uintptr_t loot_tables = 0x89f8;  // std::vector<LootTable>, loottables.json
}  // namespace globals_offset

// The ball, as the game reaches it from bubble_pickup's caller and from its position checks.
inline constexpr uintptr_t world_ball_offset = 0x830;  // World -> Object*, the ball
inline constexpr uintptr_t ball_body_offset = 0x18;    // ball -> physics body
inline constexpr uintptr_t body_position_offset = 0xc;  // physics body -> float x, y, in metres
inline constexpr float physics_to_level = 100.0f;      // metres -> level units

// Every level object keeps a weak pointer to itself, which the game locks when the object becomes another's owner.
inline constexpr uintptr_t object_self_control_offset = 0x10;  // _Ref_count_base*; uses at +8
inline constexpr uintptr_t object_level_offset = 0x80;         // the level object

// Box2D: a level's physics world refuses to create bodies while it steps, and the tick can run inside a step.
inline constexpr uintptr_t level_physics_offset = 0x2c0;     // b2World*
inline constexpr uintptr_t physics_flags_offset = 0x19298;   // b2World::m_flags
inline constexpr uint32_t physics_locked = 2;                // e_locked

// Offsets into a pickup_item level object, `this` of pickup_dialog. Its location id is level_number << 16 | id.
namespace pickup_offset {
inline constexpr uintptr_t id = 0x20;     // uint32, the object id within its level
inline constexpr uintptr_t level = 0x80;  // the level object
inline constexpr uintptr_t item = 0x230;  // std::string, the item id
}  // namespace pickup_offset
inline constexpr uintptr_t level_info_offset = 0x6e0;   // level object -> level info
inline constexpr uintptr_t level_number_offset = 0x20;  // level info -> uint32 level number

// The marker the patcher puts into `quests_completed`, followed by the configuration identifier.
inline constexpr std::string_view kIdentifierPrefix = "@open-yoku-rando:";
// The marker that holds Randovania's layout UUID, followed by the UUID.
inline constexpr std::string_view kLayoutUuidPrefix = "@open-yoku-rando:uuid:";

// Item id of a location holding nothing the game knows; see docs/multiworld-state.md.
inline constexpr const char* nothing_item = "nothing";
// Also true for the "nothing_<model>" ids the patcher mints for a custom bubble model.
bool is_nothing_item(const std::string& item);

// One element of globals_offset::randomizer_entries, the save's `_global.randomizer`. Based on YokuArchipelagoMod's
// RandomizerItem; verified in docs/multiworld-state.md.
struct RandomizerEntry {
    int32_t id;  // level number << 16 | object id
    uint8_t unknown_0[0x4];
    std::string vanilla_item;  // filled from the CSV on every level load; not saved
    std::string item;          // the placed item id
    int32_t tracker_x;
    int32_t tracker_y;
    bool revealed;
    bool collected;
    uint8_t unknown_1[0x16];
};
static_assert(sizeof(RandomizerEntry) == 0x68);
static_assert(offsetof(RandomizerEntry, vanilla_item) == 0x08);
static_assert(offsetof(RandomizerEntry, item) == 0x28);
static_assert(offsetof(RandomizerEntry, revealed) == 0x50);
static_assert(offsetof(RandomizerEntry, collected) == 0x51);

// One element of globals_offset::inventory, as getItemCount (0x202c10) reads it.
struct InventoryItem {
    float unknown_0;
    float unknown_1;
    std::string item;
    void* image;
    void* unknown_2;
    int32_t count;
    std::string description;
};
static_assert(sizeof(InventoryItem) == 0x60);
static_assert(offsetof(InventoryItem, item) == 0x08);
static_assert(offsetof(InventoryItem, count) == 0x38);

// A field of a game object at a byte offset.
template <class T>
T& field(void* object, uintptr_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// Finds out which build is running and checks that nothing else patched it. Empty on success.
std::string verify();
// The running build; only valid once verify() succeeded.
const Build& build();

uint8_t* base();
uint8_t* globals();  // null until the game has set up its globals

// Empty before the game has globals.
std::string current_level();
// False in the main menu, where items must not be given.
bool in_game();
// Counts simulated frames: stands still while paused, loading or in a menu, unlike the tick.
uint32_t frame_counter();

// The game's per-location state; empty before a save is loaded. Survives level changes, so it is always the full
// collected set.
std::span<RandomizerEntry> randomizer_entries();
// level number << 16 | object id of a pickup_item; 0 if it has no level yet.
uint32_t pickup_location(void* pickup);

std::span<const InventoryItem> inventory();
// Same value as the getItemCount native.
int32_t item_count(const std::string& item);

// The save's `quests_completed`; null before the globals exist.
std::vector<std::string>* quests_completed();

void inventory_add(const std::string& item);

struct Ball {
    uint8_t* object;
    uint8_t* level;
    float x;  // level units
    float y;
};
// The ball, if the game has one that can own objects.
std::optional<Ball> ball();

// Later: the physics world is busy; try again on another tick.
enum class LootResult { NotLoot, Given, Later, Failed };
// If `name` is a loot table (fruit), spawns it as a bubble at the ball and lets the ball take it at once, so the game
// hands out the fruit with its own rules. NotLoot for every other item.
LootResult give_loot(const std::string& name);
// The game's runner of one `onpickup` rule (see item_rules.cpp): applies `action` to `target` if that level is
// loaded, else queues it until the level loads. Reads the main map through the level being played.
void item_rule_action(void* state, int32_t target, const char* action);
// Takes one copy of `item` out of the inventory. False if none was held.
bool inventory_remove(const std::string& item);
void set_string(const std::string& key, const std::string& value);
void erase_string(const std::string& key);
// The dialog string for key, or empty; `found` tells the two apart.
std::string get_string(const std::string& key, bool* found = nullptr);
// Adds `entries` to the dialog table. Cheap per tick: only rechecks when the table was replaced or resized.
void ensure_dialog_strings(const std::vector<std::pair<std::string, std::string>>& entries);
// Adds a "ui_" key unless present. False while the UI text is not loaded yet.
bool ensure_ui_string(const std::string& key, const std::string& value);
// Adds to `fruit` and `total_fruit`, like addFruit with a positive amount.
void add_fruit(int32_t amount);
bool dialog_open();
void show_dialog(const std::string& title, const std::string& text);

// The `onpickup <item>` rules of randomizer_scripts.csv, see item_rules.cpp.
// For a pickup of the game's own: mark_collected ran the rules already, so only the save marker is set, unless the
// game queued an action.
void note_item_rules_ran(const std::string& item);
// Per tick: runs the rules of held items that still need them, only when `running`.
void check_item_rules(bool running);

// Progressive pickups, see progressive.cpp. Before pickup_dialog: turns a base stage that is already held into its
// upgrade and returns the base stage, else empty. After it: removes that base stage.
std::string upgrade_pickup_stage(void* pickup);
void drop_replaced_stage(const std::string& base);

// Location texts at pickup, see pickup_text.cpp.
// Called before pickup_dialog runs: puts the location's line in place and collapses "nothing_<model>" to "nothing".
void on_bubble_pickup(void* pickup);
// The dialog name pickup_dialog should open; valid until end_bubble_pickup.
const char* pickup_dialog_name(const char* game_name);
void end_bubble_pickup();
// Per tick: queues the line of a location collected without pickup_dialog (a Wickerling).
void watch_collected_locations();
std::optional<std::pair<std::string, std::string>> pop_location_dialog();

struct Native {
    std::string name;
    uintptr_t rva;
    uintptr_t registration;  // its `lea rdx, [fn]` in script_api_init
};
// The script natives, read from script_api_init's registration code.
std::vector<Native> find_natives();

void* lua_newstate();
void luaL_openlibs(void* state);
void lua_setglobal(void* state, const char* name);
void lua_pushstring(void* state, const char* text);
bool run_script(void* context, const char* source);

}  // namespace oyr::game
