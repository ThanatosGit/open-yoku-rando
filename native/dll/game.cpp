// inventory_add, set_string and show_dialog are based on YokuArchipelagoMod (MIT, see THIRD_PARTY.md).

#include "game.hpp"

#include "log.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <intrin.h>
#include <map>

namespace oyr::game {
namespace {

using StringTable = std::vector<std::array<std::string, 2>>;
using InventoryAddFn = void* (*)(void* inventory, const char* item);
using InventoryRemoveFn = bool (*)(void* inventory, const char* item);
using DialogInitFn = void (*)(void* table, void* unknown, void* unknown2, const char* dialog);
using DialogLineFn = void* (*)(void* table, int64_t line, void* unknown);
using LuaAllocFn = void* (*)(void* ud, void* ptr, size_t old_size, size_t new_size);
using LuaNewStateFn = void* (*)(LuaAllocFn alloc, void* ud);
using LuaStateFn = void (*)(void* state);
using LuaSetGlobalFn = void (*)(void* state, const char* name);
using LuaPushStringFn = void (*)(void* state, const char* text);
using RunScriptFn = bool (*)(void* context, const char* source);
using ItemRuleActionFn = void (*)(void* state, int32_t target, const char* action);

// MSVC's std::shared_ptr: the object, then the control block (vtable, uses at +8, weaks at +0xc).
struct SharedPtr {
    void* object = nullptr;
    uint8_t* control = nullptr;
};
using LootTableFindFn = void* (*)(void* tables, const char* name);
using LootSpawnFn = SharedPtr* (*)(SharedPtr* out, void* level, void* spawner, void* table, const float* position, void* owner);
using BubblePickupFn = void (*)(void* bubble, void* collector);

// Drops our reference, as the game's own code does when a shared_ptr goes out of scope: the object dies with its last
// reference, the control block with its last weak one. Call it once per reference the game handed us, after we are
// done with the object; the level keeps its own reference, so this usually only counts down.
void release(SharedPtr& pointer) {
    uint8_t* control = pointer.control;
    if (!control) {
        return;
    }
    using Method = void (*)(void*);
    Method* vtable = *reinterpret_cast<Method**>(control);
    if (_InterlockedDecrement(reinterpret_cast<volatile long*>(control + 8)) == 0) {
        vtable[0](control);
        if (_InterlockedDecrement(reinterpret_cast<volatile long*>(control + 0xc)) == 0) {
            vtable[1](control);
        }
    }
    pointer = {};
}


template <class Fn, uintptr_t Build::* rva>
Fn game_fn() {
    return reinterpret_cast<Fn>(base() + build().*rva);
}

// The game's std::string and std::vector have the same ABI as ours; both are MSVC x64.
StringTable* strings_of(uintptr_t file_offset) {
    uint8_t* data = globals();
    uint8_t* file = data ? field<uint8_t*>(data, file_offset) : nullptr;
    return file ? &field<StringTable>(file, globals_offset::file_strings) : nullptr;
}

template <class T>
std::span<T> vector_at(uintptr_t offset) {
    uint8_t* data = globals();
    if (!data) {
        return {};
    }
    return field<std::vector<T>>(data, offset);
}

const char* inventory_id(const std::string& item) {
    return item == "wickerling" ? "collectible" : item.c_str();
}

}  // namespace

uint8_t* globals() {
    return *reinterpret_cast<uint8_t**>(base() + build().globals_ptr);
}

std::string current_level() {
    uint8_t* data = globals();
    return data ? field<std::string>(data, globals_offset::level) : std::string();
}

bool in_game() {
    std::string level = current_level();
    return !level.empty() && level != "_start_screen";
}

uint32_t frame_counter() {
    uint8_t* data = globals();
    return data ? field<uint32_t>(data, globals_offset::frame_counter) : 0;
}

std::span<RandomizerEntry> randomizer_entries() {
    return vector_at<RandomizerEntry>(globals_offset::randomizer_entries);
}

uint32_t pickup_location(void* pickup) {
    auto* level = field<uint8_t*>(pickup, pickup_offset::level);
    uint8_t* info = level ? field<uint8_t*>(level, level_info_offset) : nullptr;
    if (!info) {
        return 0;
    }
    return (field<uint32_t>(info, level_number_offset) << 16) | field<uint32_t>(pickup, pickup_offset::id);
}

std::span<const InventoryItem> inventory() {
    return vector_at<InventoryItem>(globals_offset::inventory);
}

int32_t item_count(const std::string& item) {
    const std::string_view id = inventory_id(item);
    for (const InventoryItem& held : inventory()) {
        if (held.item == id) {
            return held.count;
        }
    }
    return 0;
}

std::vector<std::string>* quests_completed() {
    uint8_t* data = globals();
    return data ? &field<std::vector<std::string>>(data, globals_offset::quests_completed) : nullptr;
}

void inventory_add(const std::string& item) {
    game_fn<InventoryAddFn, &Build::inventory_add>()(globals() + globals_offset::inventory, inventory_id(item));
}

std::optional<Ball> ball() {
    uint8_t* data = globals();
    uint8_t* world = data ? field<uint8_t*>(data, globals_offset::world) : nullptr;
    uint8_t* object = world ? field<uint8_t*>(world, world_ball_offset) : nullptr;
    uint8_t* body = object ? field<uint8_t*>(object, ball_body_offset) : nullptr;
    uint8_t* level = object ? field<uint8_t*>(object, object_level_offset) : nullptr;
    uint8_t* self = object ? field<uint8_t*>(object, object_self_control_offset) : nullptr;
    // The game takes a reference to the ball through its weak self pointer and crashes if the ball is already dying.
    if (!body || !level || !self || field<int32_t>(self, 8) <= 0) {
        return std::nullopt;
    }
    return Ball{object, level, field<float>(body, body_position_offset) * physics_to_level,
                field<float>(body, body_position_offset + 4) * physics_to_level};
}

LootResult give_loot(const std::string& name) {
    uint8_t* data = globals();
    void* table = data ? game_fn<LootTableFindFn, &Build::loot_table_find>()(data + globals_offset::loot_tables, name.c_str()) : nullptr;
    if (!table) {
        return LootResult::NotLoot;
    }
    std::optional<Ball> at = ball();
    if (!at) {
        log::info("cannot spawn loot {}: no ball", name);
        return LootResult::Failed;
    }
    uint8_t* physics = field<uint8_t*>(at->level, level_physics_offset);
    if (!physics) {
        log::info("cannot spawn loot {}: the level has no physics world", name);
        return LootResult::Failed;
    }
    // The bubble gets a body; while the world steps, it would get none and the game would crash on it.
    if (field<uint32_t>(physics, physics_flags_offset) & physics_locked) {
        return LootResult::Later;
    }
    const float position[2] = {at->x, at->y};
    uint8_t* info = field<uint8_t*>(at->level, level_info_offset);
    log::info("spawning loot {} at {:.0f},{:.0f} in level {}", name, position[0], position[1],
              info ? field<uint32_t>(info, level_number_offset) : 0);

    SharedPtr bubble;
    // The ball is the spawner: the game makes it the bubble's owner.
    game_fn<LootSpawnFn, &Build::loot_spawn>()(&bubble, at->level, at->object, table, position, nullptr);
    if (!bubble.object) {
        log::info("loot {} spawned no bubble", name);
        return LootResult::Failed;
    }
    // As the game does for a bubble its owner hands out at once.
    game_fn<BubblePickupFn, &Build::bubble_pickup>()(bubble.object, at->object);
    release(bubble);
    return LootResult::Given;
}

void item_rule_action(void* state, int32_t target, const char* action) {
    game_fn<ItemRuleActionFn, &Build::item_rule_action>()(state, target, action);
}

bool inventory_remove(const std::string& item) {
    return game_fn<InventoryRemoveFn, &Build::inventory_remove>()(globals() + globals_offset::inventory, inventory_id(item));
}

void set_string(const std::string& key, const std::string& value) {
    StringTable& strings = *strings_of(key.starts_with("ui_") ? globals_offset::ui_file : globals_offset::dialog_file);
    for (auto& entry : strings) {
        if (entry[0] == key) {
            entry[1] = value;
            return;
        }
    }
    strings.push_back({key, value});
}

void erase_string(const std::string& key) {
    std::erase_if(*strings_of(globals_offset::dialog_file), [&key](const auto& entry) { return entry[0] == key; });
}

std::string get_string(const std::string& key, bool* found) {
    for (const auto& entry : *strings_of(globals_offset::dialog_file)) {
        if (entry[0] == key) {
            if (found) {
                *found = true;
            }
            return entry[1];
        }
    }
    if (found) {
        *found = false;
    }
    return {};
}

void ensure_dialog_strings(const std::vector<std::pair<std::string, std::string>>& entries) {
    static const void* checked_data = nullptr;
    static size_t checked_size = 0;
    StringTable* table = strings_of(globals_offset::dialog_file);
    if (entries.empty() || !table) {
        return;
    }
    StringTable& strings = *table;
    if (strings.data() == checked_data && strings.size() == checked_size) {
        return;
    }
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < strings.size(); ++i) {
        index.emplace(strings[i][0], i);
    }
    size_t changed = 0;
    for (const auto& [key, value] : entries) {
        if (auto existing = index.find(key); existing == index.end()) {
            strings.push_back({key, value});
            ++changed;
        } else if (strings[existing->second][1] != value) {
            strings[existing->second][1] = value;
            ++changed;
        }
    }
    if (changed > 0) {
        log::info("set {} of the mod's {} texts in the dialog table", changed, entries.size());
    }
    checked_data = strings.data();
    checked_size = strings.size();
}

bool ensure_ui_string(const std::string& key, const std::string& value) {
    StringTable* strings = strings_of(globals_offset::ui_file);
    if (!strings) {
        return false;
    }
    if (std::ranges::none_of(*strings, [&key](const auto& entry) { return entry[0] == key; })) {
        strings->push_back({key, value});
    }
    return true;
}

void add_fruit(int32_t amount) {
    field<int32_t>(globals(), globals_offset::fruit) += amount;
    field<int32_t>(globals(), globals_offset::total_fruit) += amount;
}

bool dialog_open() {
    return field<bool>(globals(), globals_offset::dialog_flag);
}

void show_dialog(const std::string& title, const std::string& text) {
    set_string("oyr_dialog_name", title);
    set_string("oyr_dialog_1", text);

    void* table = globals() + globals_offset::string_table;
    // Zeroed memory as the second argument, as YokuArchipelagoMod does; what it is is unknown.
    static std::array<uint8_t, 0x60> unknown{};

    field<bool>(globals(), globals_offset::dialog_flag) = true;
    game_fn<DialogInitFn, &Build::dialog_init>()(table, unknown.data(), nullptr, "oyr_dialog");
    game_fn<DialogLineFn, &Build::dialog_line>()(table, 1, nullptr);
}

void* lua_newstate() {
    auto alloc = game_fn<LuaAllocFn, &Build::lua_alloc>();
    return game_fn<LuaNewStateFn, &Build::lua_newstate>()(alloc, nullptr);
}

void luaL_openlibs(void* state) {
    game_fn<LuaStateFn, &Build::luaL_openlibs>()(state);
}

void lua_setglobal(void* state, const char* name) {
    game_fn<LuaSetGlobalFn, &Build::lua_setglobal>()(state, name);
}

void lua_pushstring(void* state, const char* text) {
    game_fn<LuaPushStringFn, &Build::lua_pushstring>()(state, text);
}

bool run_script(void* context, const char* source) {
    return game_fn<RunScriptFn, &Build::run_script>()(context, source);
}

}  // namespace oyr::game
