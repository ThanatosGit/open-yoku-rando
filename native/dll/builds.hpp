#pragma once

#include <array>
#include <cstdint>

namespace oyr::game {

// The addresses that differ between the Epic, GOG and Steam builds.
struct Build {
    const char* name;
    // PE TimeDateStamp; readable before Steam's DRM has decrypted .text
    uint32_t timestamp;
    // the byte at check_byte once .text is decrypted
    uint8_t check_byte_value;
    uintptr_t globals_ptr;
    // zero padding at the end of .text
    uintptr_t code_cave;
    uintptr_t code_cave_end;

    // void*(Inventory*, const char* item)
    uintptr_t inventory_add;
    // bool(Inventory*, const char* item): removes one copy, false if none is held.
    uintptr_t inventory_remove;
    uintptr_t dialog_init;
    uintptr_t dialog_line;
    // called every tick through these vtable slots; also runs bubble pickups
    uintptr_t tick;
    std::array<uintptr_t, 3> tick_vtable_slots;

    // Bubble method run when the ball takes the item: inventory_add, then the dialog "items/<item>".
    uintptr_t pickup_dialog;
    uintptr_t pickup_dialog_vtable_slot;
    // its call to dialog_init; 4th argument is the dialog name
    uintptr_t pickup_dialog_init_call;
    // In the slot menu's label builder: sprintf(buf, " (%s: %i)", difficulty name, seed).
    uintptr_t slot_label_format_call;

    // Beacon method run when the ball uses it: with at least 10 Wickerlings held, takes 10 and lights the beacon.
    uintptr_t beacon_use;
    uintptr_t beacon_use_vtable_slot;

    // reads and writes the whole save
    uintptr_t save_load;
    // calls mark_collected, then the inventory add for Wickerlings
    uintptr_t bubble_pickup;
    // LootTable*(std::vector<LootTable>* tables, const char* name): the loot table of that name, or null.
    uintptr_t loot_table_find;
    // shared_ptr<Bubble>*(shared_ptr<Bubble>* out, Level*, Object* spawner, LootTable*, const float* position,
    // Object* owner): a loot bubble at the position, added to the level; what a fruit location spawns.
    uintptr_t loot_spawn;
    // bool(RandomizerState*, pickup_item*, ...): sets the pickup's collected flag.
    uintptr_t mark_collected;
    // void(RandomizerState*, int32 target, const char* action): what mark_collected runs for each `onpickup <item>`
    // rule of randomizer_scripts.csv; queues the action when the target's level is not loaded.
    uintptr_t item_rule_action;

    // Creates a scripted object's own lua_State and registers the natives; find_natives reads them from here.
    uintptr_t script_api_init;
    uintptr_t script_api_init_end;
    // bool(ScriptContext*, const char* source): lua_load + pcall
    uintptr_t run_script;

    // Lua 5.2
    uintptr_t lua_newstate;
    // the allocator the game passes to lua_newstate
    uintptr_t lua_alloc;
    uintptr_t luaL_openlibs;
    uintptr_t lua_setglobal;
    uintptr_t lua_pushstring;

    // Instructions verify() checks to prove the offsets the DLL reads; see kAnchors in verify.cpp.
    // in save_load
    uintptr_t anchor_randomizer_entries;
    // in the randomizer serializer
    uintptr_t anchor_collected_flag;
    // in mark_collected
    uintptr_t anchor_entry_stride;
    // in mark_collected
    uintptr_t anchor_collected_store;
    // in bubble_pickup
    uintptr_t anchor_randomizer_state;
    // in mark_collected
    uintptr_t anchor_item_rules;

    // Runs on every level load of a randomizer game; rolls the placement only while the entry vector is empty.
    // (RandomizerState*, level data, bool dump); see docs/exe-research.md.
    uintptr_t randomizer_loader;
    uintptr_t randomizer_loader_call;

    // The slot menu builder, where an empty slot gets its entries.
    // lea r9, "ui_start_game"
    uintptr_t menu_start_text;
    // lea r8, "slot_new_"
    uintptr_t menu_start_command;
    // mov rcx, [globals] before the "Randomize mode" submenu
    uintptr_t menu_skip_from;
    // mov rcx, [globals] before the "Back" entry
    uintptr_t menu_skip_to;
    // "slot_newrandom_"
    uintptr_t menu_newrandom_string;
};

inline constexpr uintptr_t check_byte = 0x203154;

// 7,427,584 bytes, sha256 71dac58e...787c.
inline constexpr Build kEpic{
    .name = "Epic",
    .timestamp = 0x615036bf,
    .check_byte_value = 0x48,
    .globals_ptr = 0x5455e8,
    .code_cave = 0x41231f,
    .code_cave_end = 0x413000,
    .inventory_add = 0x296bc0,
    .inventory_remove = 0x297210,
    .dialog_init = 0x28b7a0,
    .dialog_line = 0x28be70,
    .tick = 0x25c280,
    .tick_vtable_slots = {0x4af8b8, 0x4bbf18, 0x4bdab0},
    .pickup_dialog = 0x1f4110,
    .pickup_dialog_vtable_slot = 0x4b22e8,
    .pickup_dialog_init_call = 0x1f4520,
    .slot_label_format_call = 0x268fa4,
    .beacon_use = 0x216450,
    .beacon_use_vtable_slot = 0x4b5f50,
    .save_load = 0x26b0c0,
    .bubble_pickup = 0x164da0,
    .loot_table_find = 0x166620,
    .loot_spawn = 0x163040,
    .mark_collected = 0x16b040,
    .item_rule_action = 0x16b640,
    .script_api_init = 0x205020,
    .script_api_init_end = 0x2062e7,
    .run_script = 0x293f0,
    .lua_newstate = 0xe4a40,
    .lua_alloc = 0x29820,
    .luaL_openlibs = 0xd9880,
    .lua_setglobal = 0xcb180,
    .lua_pushstring = 0xcae70,
    .anchor_randomizer_entries = 0x26b3ee,
    .anchor_collected_flag = 0x16c0d7,
    .anchor_entry_stride = 0x16b0a5,
    .anchor_collected_store = 0x16b45e,
    .anchor_randomizer_state = 0x164ee0,
    .anchor_item_rules = 0x16b182,
    .randomizer_loader = 0x168da0,
    .randomizer_loader_call = 0x2591b2,
    .menu_start_text = 0x2ae0ec,
    .menu_start_command = 0x2ae11b,
    .menu_skip_from = 0x2ae1a6,
    .menu_skip_to = 0x2aebfb,
    .menu_newrandom_string = 0x4c0700,
};

// 7,423,488 bytes. Game code is Epic's shifted by -0x750, the Lua library is where Epic has it.
inline constexpr Build kGog{
    .name = "GOG",
    .timestamp = 0x61503c9f,
    .check_byte_value = 0x2b,
    .globals_ptr = 0x5435e8,
    .code_cave = 0x411b6f,
    .code_cave_end = 0x412000,
    .inventory_add = 0x296470,
    .inventory_remove = 0x296ac0,
    .dialog_init = 0x28b050,
    .dialog_line = 0x28b720,
    .tick = 0x25bb30,
    .tick_vtable_slots = {0x4ae690, 0x4bacc0, 0x4bc830},
    .pickup_dialog = 0x1f39c0,
    .pickup_dialog_vtable_slot = 0x4b0c00,
    .pickup_dialog_init_call = 0x1f3dd0,
    .slot_label_format_call = 0x268854,
    .beacon_use = 0x215d00,
    .beacon_use_vtable_slot = 0x4b4d18,
    .save_load = 0x26a970,
    .bubble_pickup = 0x164650,
    .loot_table_find = 0x165ed0,
    .loot_spawn = 0x1628f0,
    .mark_collected = 0x16a8f0,
    .item_rule_action = 0x16aef0,
    .script_api_init = 0x2048d0,
    .script_api_init_end = 0x205b97,
    .run_script = 0x293f0,
    .lua_newstate = 0xe4a40,
    .lua_alloc = 0x29820,
    .luaL_openlibs = 0xd9880,
    .lua_setglobal = 0xcb180,
    .lua_pushstring = 0xcae70,
    .anchor_randomizer_entries = 0x26ac9e,
    .anchor_collected_flag = 0x16b987,
    .anchor_entry_stride = 0x16a955,
    .anchor_collected_store = 0x16ad0e,
    .anchor_randomizer_state = 0x164790,
    .anchor_item_rules = 0x16aa32,
    .randomizer_loader = 0x168650,
    .randomizer_loader_call = 0x258a62,
    .menu_start_text = 0x2ad97c,
    .menu_start_command = 0x2ad9ab,
    .menu_skip_from = 0x2ada36,
    .menu_skip_to = 0x2ae48b,
    .menu_newrandom_string = 0x4bf4e0,
};

// 7,664,312 bytes, wrapped in SteamStub (.text encrypted on disk). Mapped on the decrypted image: game code shifted
// by +0x700 (below 0x250000) and +0x6c0 (above), most of the Lua library by -0x60.
inline constexpr Build kSteam{
    .name = "Steam",
    .timestamp = 0x61502fec,
    .check_byte_value = 0x07,
    .globals_ptr = 0x5456a8,
    .code_cave = 0x4129c0,
    .code_cave_end = 0x413000,
    .inventory_add = 0x297280,
    .inventory_remove = 0x2978d0,
    .dialog_init = 0x28be60,
    .dialog_line = 0x28c530,
    .tick = 0x25c940,
    .tick_vtable_slots = {0x4afc88, 0x4bc000, 0x4bdb70},
    .pickup_dialog = 0x1f4810,
    .pickup_dialog_vtable_slot = 0x4b1f38,
    .pickup_dialog_init_call = 0x1f4c20,
    .slot_label_format_call = 0x269664,
    .beacon_use = 0x216b50,
    .beacon_use_vtable_slot = 0x4b5e10,
    .save_load = 0x26b780,
    .bubble_pickup = 0x1654a0,
    .loot_table_find = 0x166d20,
    .loot_spawn = 0x163740,
    .mark_collected = 0x16b740,
    .item_rule_action = 0x16bd40,
    .script_api_init = 0x205720,
    .script_api_init_end = 0x2069e7,
    .run_script = 0x293f0,
    .lua_newstate = 0xe49e0,
    .lua_alloc = 0x29820,
    .luaL_openlibs = 0xd9820,
    .lua_setglobal = 0xcb120,
    .lua_pushstring = 0xcae10,
    .anchor_randomizer_entries = 0x26baae,
    .anchor_collected_flag = 0x16c7d7,
    .anchor_entry_stride = 0x16b7a5,
    .anchor_collected_store = 0x16bb5e,
    .anchor_randomizer_state = 0x1655e0,
    .anchor_item_rules = 0x16b882,
    .randomizer_loader = 0x1694a0,
    .randomizer_loader_call = 0x259872,
    .menu_start_text = 0x2ae7ac,
    .menu_start_command = 0x2ae7db,
    .menu_skip_from = 0x2ae866,
    .menu_skip_to = 0x2af2bb,
    .menu_newrandom_string = 0x4c0850,
};

inline constexpr std::array<const Build*, 3> kBuilds{&kEpic, &kGog, &kSteam};

}  // namespace oyr::game
