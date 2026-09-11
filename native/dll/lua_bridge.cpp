#include "lua_bridge.hpp"

#include "game.hpp"
#include "goal.hpp"
#include "log.hpp"
#include "messages.hpp"
#include "multiworld.hpp"
#include "overlay.hpp"
#include "new_game.hpp"
#include "remote_api.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oyr::bridge {
namespace {

using game::field;

// Lua 5.2 as compiled into Yoku.exe; the offsets match the exe's own code (debug.debug, lua_newstate, the natives).
struct TValue {
    void* value;
    int32_t tt;
};
static_assert(sizeof(TValue) == 0x10);

constexpr int32_t kTypeLightCFunction = 0x16;  // LUA_TLCF
constexpr int32_t kTypeString = 4;             // LUA_TSTRING in the low nibble of tt
constexpr size_t kStateTop = 0x10;
constexpr size_t kStateCallInfo = 0x20;
constexpr size_t kStateStackLast = 0x30;
constexpr size_t kStateCurrentObject = 0xd0;  // field Yoku adds to lua_State; natives of scripted objects read it
constexpr size_t kCallInfoFunc = 0x00;
constexpr size_t kStringLength = 0x10;
constexpr size_t kStringData = 0x18;

using lua_State = void;
using lua_CFunction = int (*)(lua_State*);

// Natives that only touch global game state (checked in the exe); the others need the current object [L+0xd0],
// which the DLL's state doesn't have.
constexpr std::array<std::string_view, 5> kSafeNatives{"getFruit", "addFruit", "getItemCount", "hasItem", "getGameMode"};

// What run_script expects: the lua_State* first. The game's own contexts are larger, so leave zeroed room.
struct ScriptContext {
    void* state = nullptr;
    std::array<uint8_t, 0xf8> reserved{};
};

// One queued inventory change. Removing is a test operation and shows no dialog.
struct ItemRequest {
    std::string item;  // on a give, a progression: item ids from the lowest stage up, separated by commas
    std::string text;  // shown as a message once the item is given
    bool remove = false;
    int32_t amount = 0;  // on a remove, how many copies to take; 0 means every copy held
    // On a give from Randovania, the received count and inventory index it expects; dropped on any other value.
    std::optional<size_t> received;
    std::optional<size_t> inventory;
};

struct Result {
    bool received = false;
    bool success = false;
    std::string value;
};

std::atomic<DWORD> g_game_thread = 0;

// Ticks without a new frame that still count as running, for RL.Running().
constexpr uint32_t kRunningTicks = 8;

// What RL.BufferSize reports: the chunk size Randovania's bootstrap splits on. The socket accepts more.
constexpr uint32_t kBufferSize = 65536;

// Only touched on the game thread.
ScriptContext g_context;
bool g_state_failed = false;
Result g_result;
std::deque<ItemRequest> g_items;
uint32_t g_last_frame = 0;
bool g_have_frame = false;
uint32_t g_stalled_ticks = kRunningTicks;

// Argument `index` (1-based) of the running C function, or an empty string if it isn't a string.
std::string string_arg(lua_State* L, int index) {
    TValue* arg = field<TValue*>(field<void*>(L, kStateCallInfo), kCallInfoFunc) + index;
    if (arg >= field<TValue*>(L, kStateTop) || (arg->tt & 0x0f) != kTypeString) {
        return {};
    }
    char* string = static_cast<char*>(arg->value);
    return std::string(string + kStringData, field<size_t>(string, kStringLength));
}

int rl_result(lua_State* L) {
    g_result = {true, string_arg(L, 1) == "1", string_arg(L, 2)};
    return 0;
}

int rl_log(lua_State* L) {
    log::info("lua: {}", string_arg(L, 1));
    return 0;
}

// The stage of a progression to give: the one above the highest stage held, or the last one again. In Yoku an upgrade
// replaces the stage below it (the game's own pickups do that in 0x16b040), so that stage comes back to be removed.
struct Stage {
    std::string give;
    std::string replaces;
};

Stage next_stage(std::string_view progression) {
    std::vector<std::string> stages;
    for (size_t start = 0;;) {
        size_t comma = progression.find(',', start);
        stages.emplace_back(progression.substr(start, comma - start));
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    size_t next = 0;
    for (size_t i = stages.size(); i-- > 0;) {
        if (game::item_count(stages[i]) > 0) {
            next = i + 1;
            break;
        }
    }
    if (next == 0) {
        return {stages.front(), {}};
    }
    if (next == stages.size()) {
        return {stages.back(), {}};
    }
    return {stages[next], stages[next - 1]};
}

// A number passed as a string, like RemoveItem's amount; empty when it is missing or not a number.
std::optional<size_t> size_arg(lua_State* L, int index) {
    const std::string text = string_arg(L, index);
    size_t value = 0;
    if (text.empty() || std::from_chars(text.data(), text.data() + text.size(), value).ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

// RL.GiveItem(item, text [, received, inventory])
int rl_give_item(lua_State* L) {
    ItemRequest request{string_arg(L, 1), string_arg(L, 2)};
    request.received = size_arg(L, 3);
    request.inventory = size_arg(L, 4);
    g_items.push_back(std::move(request));
    return 0;
}

int rl_show_message(lua_State* L) {
    messages::push(string_arg(L, 1));
    return 0;
}

// Whether a give from Randovania was named from the state the game is still in.
bool still_current(const ItemRequest& request) {
    if ((request.received || request.inventory) && !multiworld::save_matches()) {
        return false;
    }
    if (request.received && *request.received != multiworld::received_count()) {
        return false;
    }
    return !request.inventory || *request.inventory == multiworld::inventory_index();
}

// RL.RemoveItem(item [, amount]): the amount comes as a string, because that is what string_arg reads.
int rl_remove_item(lua_State* L) {
    const std::string amount = string_arg(L, 2);
    int32_t count = 0;
    std::from_chars(amount.data(), amount.data() + amount.size(), count);
    g_items.push_back({string_arg(L, 1), {}, true, std::max(count, 0)});
    return 0;
}

int rl_level(lua_State* L) {
    game::lua_pushstring(L, game::current_level().c_str());
    return 1;
}

int rl_running(lua_State* L) {
    game::lua_pushstring(L, g_stalled_ticks < kRunningTicks ? "1" : "");
    return 1;
}

int rl_set_text(lua_State* L) {
    game::set_string(string_arg(L, 1), string_arg(L, 2));
    return 0;
}

// Randovania sends the order of packet 6's bits and packet 5's counts itself, so it never depends on the CSV order.
int rl_set_locations(lua_State* L) {
    game::lua_pushstring(L, std::to_string(multiworld::set_locations(string_arg(L, 1))).c_str());
    return 1;
}

int rl_set_inventory_items(lua_State* L) {
    game::lua_pushstring(L, std::to_string(multiworld::set_inventory_items(string_arg(L, 1))).c_str());
    return 1;
}

int rl_set_identifier(lua_State* L) {
    multiworld::set_identifier(string_arg(L, 1));
    return 0;
}

int rl_received(lua_State* L) {
    game::lua_pushstring(L, std::to_string(multiworld::received_count()).c_str());
    return 1;
}

int rl_layout_uuid(lua_State* L) {
    game::lua_pushstring(L, multiworld::layout_uuid().c_str());
    return 1;
}

constexpr std::array<std::pair<const char*, lua_CFunction>, 13> kFunctions{{
    {"__rl_result", rl_result},
    {"__rl_log", rl_log},
    {"__rl_give_item", rl_give_item},
    {"__rl_show_message", rl_show_message},
    {"__rl_remove_item", rl_remove_item},
    {"__rl_set_text", rl_set_text},
    {"__rl_level", rl_level},
    {"__rl_running", rl_running},
    {"__rl_set_locations", rl_set_locations},
    {"__rl_set_inventory_items", rl_set_inventory_items},
    {"__rl_set_identifier", rl_set_identifier},
    {"__rl_received", rl_received},
    {"__rl_layout_uuid", rl_layout_uuid},
}};

// What the game's inlined lua_pushcfunction + lua_setglobal do when it registers its natives.
bool set_global_function(lua_State* L, const char* name, lua_CFunction function) {
    TValue*& top = field<TValue*>(L, kStateTop);
    if (top + 1 >= field<TValue*>(L, kStateStackLast)) {
        return false;
    }
    top->value = reinterpret_cast<void*>(function);
    top->tt = kTypeLightCFunction;
    ++top;
    game::lua_setglobal(L, name);
    return true;
}

// The DLL's own lua_State, made with the game's Lua and allocator; the game's states die with their objects.
bool create_state() {
    lua_State* L = game::lua_newstate();
    if (!L) {
        log::info("lua_newstate failed");
        return false;
    }
    field<void*>(L, kStateCurrentObject) = nullptr;
    game::luaL_openlibs(L);

    auto natives = game::find_natives();
    size_t registered = 0;
    for (const game::Native& native : natives) {
        if (std::ranges::find(kSafeNatives, native.name) != kSafeNatives.end() &&
            set_global_function(L, native.name.c_str(), reinterpret_cast<lua_CFunction>(game::base() + native.rva))) {
            ++registered;
        }
    }
    for (const auto& [name, function] : kFunctions) {
        if (!set_global_function(L, name, function)) {
            log::info("cannot register {}", name);
            return false;
        }
    }
    log::info("created lua_State {} with {} of the game's {} natives", static_cast<void*>(L), registered, natives.size());
    g_context.state = L;
    return true;
}

std::string long_string(std::string_view text) {
    std::string level;
    while (text.find("]" + level + "]") != std::string_view::npos) {
        level += '=';
    }
    // The newlines keep the text apart from the brackets; Lua drops the first one.
    return std::format("[{0}[\n{1}\n]{0}]", level, text);
}

// Runs the request's code as its own chunk, so syntax and runtime errors come back as a failed reply.
std::string wrap(std::string_view code) {
    return std::format(
        "RL = RL or {{}} RL.Version = 1 RL.BufferSize = {1} "
        "RL.GiveItem = function(item, text, received, inventory) return __rl_give_item(item, text, "
        "received and tostring(received) or \"\", inventory and tostring(inventory) or \"\") end "
        "RL.ShowMessage = __rl_show_message "
        // The amount is read as a string, so a caller writing RL.RemoveItem(item, 2) must not silently mean all.
        "RL.RemoveItem = function(item, amount) return __rl_remove_item(item, amount and tostring(amount) or \"\") end "
        "RL.SetText = __rl_set_text RL.Log = __rl_log "
        "RL.Level = __rl_level RL.InGame = function() local l = __rl_level() return l ~= \"\" and l ~= "
        "\"_start_screen\" end "
        "RL.Running = function() return __rl_running() ~= \"\" end "
        "RL.SetLocations = function(s) return tonumber(__rl_set_locations(s)) end "
        "RL.SetInventoryItems = function(s) return tonumber(__rl_set_inventory_items(s)) end "
        "RL.SetIdentifier = __rl_set_identifier "
        "RL.Received = function() return tonumber(__rl_received()) end "
        "RL.LayoutUUID = __rl_layout_uuid "
        "local f, e = load({0}, \"=remote\") "
        "if not f then __rl_result(\"0\", tostring(e)) return end "
        "local ok, v = pcall(f) "
        "if ok then __rl_result(\"1\", v == nil and \"\" or tostring(v)) else __rl_result(\"0\", tostring(v)) end",
        long_string(code), kBufferSize);
}

void run_remote_lua(const remote::LuaRequest& request) {
    if (!g_context.state && !g_state_failed && !create_state()) {
        g_state_failed = true;
    }
    if (!g_context.state) {
        remote::reply_lua(request.connection, false, "the DLL could not create its Lua state");
        return;
    }

    g_result = {};
    std::string source = wrap(request.code);
    bool ran = game::run_script(&g_context, source.c_str());
    if (g_result.received) {
        remote::reply_lua(request.connection, g_result.success, g_result.value);
    } else {
        remote::reply_lua(request.connection, false, ran ? "the script sent no result" : "the script failed to run");
    }
}

}  // namespace

void on_tick() {
    // The first thread that ticks is the game thread; ticks on any other thread are ignored.
    DWORD thread = GetCurrentThreadId();
    DWORD game_thread = 0;
    if (g_game_thread.compare_exchange_strong(game_thread, thread)) {
        log::info("game thread {}", thread);
    } else if (game_thread != thread) {
        return;
    }
    if (!game::globals()) {
        return;
    }
    static bool overlay_tried = false;
    if (!overlay_tried) {
        overlay_tried = true;
        if (std::string error = overlay::install(); !error.empty()) {
            log::info("no overlay, messages are only logged: {}", error);
        } else {
            log::info("overlay hooked");
        }
    }
    // The UI text loads after the DLL starts, so the menu entry's key is added here.
    if (!game::in_game()) {
        game::ensure_ui_string(new_game::kMenuTextKey, new_game::menu_text());
    }
    game::ensure_dialog_strings(new_game::texts());

    uint32_t frame = game::frame_counter();
    if (g_have_frame && frame == g_last_frame) {
        g_stalled_ticks = std::min(g_stalled_ticks + 1, kRunningTicks);
    } else {
        g_stalled_ticks = 0;
    }
    g_last_frame = frame;
    g_have_frame = true;

    multiworld::update_inventory_index();

    // Only in a level, on a frame the game really ran (not paused) and with no dialog open.
    // A give waits for the message before it, so items arrive one at a time, each with its own message.
    if (!g_items.empty() && game::in_game() && g_stalled_ticks == 0 && !game::dialog_open() &&
        (g_items.front().remove || !messages::busy())) {
        ItemRequest request = std::move(g_items.front());
        g_items.pop_front();
        if (request.remove) {
            int32_t held = game::item_count(request.item);
            int32_t wanted = request.amount > 0 ? std::min(request.amount, held) : held;
            for (int32_t i = 0; i < wanted; ++i) {
                game::inventory_remove(request.item);
            }
            log::info("removed {} of {}, {} were held", wanted, request.item, held);
        } else if (!still_current(request)) {
            log::info("not giving {}: sent for received {} and inventory {}, the game is at {} and {}", request.item,
                      request.received.value_or(0), request.inventory.value_or(0), multiworld::received_count(),
                      multiworld::inventory_index());
            multiworld::resend_save_state();
        } else if (game::LootResult loot = game::give_loot(request.item); loot == game::LootResult::Failed) {
            // Not counted; Randovania sends it again after the resend.
            multiworld::resend_save_state();
        } else if (loot == game::LootResult::Later) {
            g_items.push_front(std::move(request));
        } else {
            // Fruit is no inventory item: the game hands it out as loot, as at a fruit location.
            if (loot == game::LootResult::NotLoot) {
                Stage stage = next_stage(request.item);
                log::info("giving {}{}", stage.give, stage.replaces.empty() ? "" : " in place of " + stage.replaces);
                game::inventory_add(stage.give);
                if (!stage.replaces.empty()) {
                    game::inventory_remove(stage.replaces);
                }
            }
            // Counted in the save, so a reload does not deliver it again.
            multiworld::note_pickup_received();
            messages::push(request.text);
        }
    }

    game::check_item_rules(g_stalled_ticks == 0 && !game::dialog_open());
    messages::on_tick(g_stalled_ticks == 0);

    // A pickup the game shows no dialog for gets its location's line from here, under the same conditions.
    game::watch_collected_locations();
    if (game::in_game() && g_stalled_ticks == 0 && !game::dialog_open()) {
        if (auto dialog = game::pop_location_dialog()) {
            game::show_dialog(dialog->first, dialog->second);
        }
    }

    multiworld::on_tick();

    if (!remote::has_pending_lua()) {
        return;
    }
    if (auto request = remote::pop_lua()) {
        run_remote_lua(*request);
    }
}

namespace {
NativeFn g_unlock_achievement = nullptr;

int hook_unlock_achievement(lua_State* L) {
    goal::on_achievement(string_arg(L, 1));
    return g_unlock_achievement(L);
}
}  // namespace

NativeFn wrap_unlock_achievement(NativeFn original) {
    g_unlock_achievement = original;
    return &hook_unlock_achievement;
}

}  // namespace oyr::bridge
