#include "hooks.hpp"

#include "game.hpp"
#include "goal.hpp"
#include "log.hpp"
#include "lua_bridge.hpp"
#include "new_game.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <format>
#include <initializer_list>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oyr::hooks {
namespace {

// Hooks take and forward all four argument registers, so the exact game signatures don't matter.
using GameFn = uint64_t (*)(void*, void*, void*, void*);

GameFn g_tick = nullptr;
GameFn g_pickup_dialog = nullptr;
GameFn g_randomizer_loader = nullptr;
GameFn g_dialog_init = nullptr;
GameFn g_slot_label_format = nullptr;
GameFn g_beacon_use = nullptr;

uint64_t hook_tick(void* a, void* b, void* c, void* d) {
    bridge::on_tick();
    return g_tick(a, b, c, d);
}

uint64_t hook_pickup_dialog(void* bubble, void* b, void* c, void* d) {
    std::string replaced = game::upgrade_pickup_stage(bubble);
    // mark_collected has run this item's onpickup rules already.
    game::note_item_rules_ran(game::field<std::string>(bubble, game::pickup_offset::item));
    game::on_bubble_pickup(bubble);
    uint64_t result = g_pickup_dialog(bubble, b, c, d);
    game::end_bubble_pickup();
    game::drop_replaced_stage(replaced);
    return result;
}

// pickup_dialog's dialog_init(table, ?, ?, dialog name): the name may be swapped for the location's own dialog.
uint64_t hook_pickup_dialog_init(void* table, void* b, void* c, void* name) {
    const char* dialog = game::pickup_dialog_name(static_cast<const char*>(name));
    return g_dialog_init(table, b, c, const_cast<char*>(dialog));
}

// A beacon is lit when its use took the Wickerlings: the game only takes them when it lights it.
uint64_t hook_beacon_use(void* beacon, void* b, void* c, void* d) {
    int32_t before = game::item_count("collectible");
    uint64_t result = g_beacon_use(beacon, b, c, d);
    goal::on_beacon_used(game::item_count("collectible") < before);
    return result;
}

uint64_t hook_randomizer_loader(void* state, void* level, void* dump, void* d) {
    bool new_game = new_game::before_loader(state);
    uint64_t result = g_randomizer_loader(state, level, dump, d);
    new_game::after_loader(state, new_game);
    return result;
}

// The slot label's sprintf(buf, " (%s: %i)", difficulty, seed): a Randovania seed shows its hash instead.
uint64_t hook_slot_label_format(void* buffer, void* format, void* difficulty, void* seed) {
    if (const std::string* hash = new_game::hash_for_seed(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(seed)))) {
        static const char kFormat[] = " - %s";
        return g_slot_label_format(buffer, const_cast<char*>(kFormat), const_cast<char*>(hash->c_str()), nullptr);
    }
    return g_slot_label_format(buffer, format, difficulty, seed);
}

bool patch(void* address, const void* bytes, size_t size, DWORD protection) {
    DWORD old;
    if (!VirtualProtect(address, size, protection, &old)) {
        return false;
    }
    std::memcpy(address, bytes, size);
    VirtualProtect(address, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    return true;
}

// Game files opened from the mod folder instead when it has them, without touching the install:
// "processed/items/<name>" from "open-yoku-rando/items/<name>" (models for the minted Nothing ids, or a replacement for
// a real item's), the randomizer's script overrides from "open-yoku-rando/randomizer_scripts.csv" and the dialog
// animations from "open-yoku-rando/dialog_anim.strings".
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kRedirects{{
    {"processed/items/", "open-yoku-rando/items/"},
    {"data/text/randomizer_scripts.csv", "open-yoku-rando/randomizer_scripts.csv"},
    {"processed/text/dialog_anim.strings", "open-yoku-rando/dialog_anim.strings"},
}};

using FopenFn = FILE* (*)(const char* path, const char* mode);
using FopenSFn = errno_t (*)(FILE** file, const char* path, const char* mode);
FopenFn g_fopen = nullptr;
FopenSFn g_fopen_s = nullptr;
std::mutex g_redirect_mutex;
std::set<std::string> g_logged_redirects;

std::string own_path(const char* path) {
    if (!path) {
        return {};
    }
    std::string original = path;
    std::string normalized = original;
    std::ranges::replace(normalized, '\\', '/');
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string own;
    for (const auto& [game_path, mod_path] : kRedirects) {
        if (size_t at = normalized.find(game_path); at != std::string::npos) {
            own = original.substr(0, at) + std::string(mod_path) + original.substr(at + game_path.size());
            break;
        }
    }
    if (own.empty() || GetFileAttributesA(own.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return {};
    }
    std::lock_guard lock(g_redirect_mutex);
    if (g_logged_redirects.insert(own).second) {
        log::info("loading {} instead of {}", own, original);
    }
    return own;
}

FILE* hook_fopen(const char* path, const char* mode) {
    std::string own = own_path(path);
    return g_fopen(own.empty() ? path : own.c_str(), mode);
}

errno_t hook_fopen_s(FILE** file, const char* path, const char* mode) {
    std::string own = own_path(path);
    return g_fopen_s(file, own.empty() ? path : own.c_str(), mode);
}

// Points the exe's import of `function` at `hook`; returns what it pointed at before, null if it imports no such name.
void* hook_import(const char* function, void* hook) {
    uint8_t* base = game::base();
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* import = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); import->Name; ++import) {
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + import->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + import->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                continue;
            }
            auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(name->Name, function) != 0) {
                continue;
            }
            auto* original = reinterpret_cast<void*>(slots->u1.Function);
            auto target = reinterpret_cast<uintptr_t>(hook);
            return patch(&slots->u1.Function, &target, sizeof(target), PAGE_READWRITE) ? original : nullptr;
        }
    }
    return nullptr;
}

// Space in the exe for absolute jumps (rel32 operands cannot reach the DLL) and for strings the exe is pointed at.
class CodeCave {
  public:
    CodeCave() : next_(game::base() + game::build().code_cave), end_(game::base() + game::build().code_cave_end) {}

    uint8_t* jump_to(void* target) {
        return place(jump_bytes(target));
    }

    // `prefix`, then a jump to target.
    uint8_t* run_then_jump(std::initializer_list<uint8_t> prefix, void* target) {
        std::vector<uint8_t> bytes(prefix);
        auto jump = jump_bytes(target);
        bytes.insert(bytes.end(), jump.begin(), jump.end());
        return place(bytes);
    }

    uint8_t* place(std::span<const uint8_t> bytes) {
        if (next_ + bytes.size() > end_ || !patch(next_, bytes.data(), bytes.size(), PAGE_EXECUTE_READWRITE)) {
            return nullptr;
        }
        uint8_t* address = next_;
        next_ += bytes.size();
        return address;
    }

  private:
    static std::array<uint8_t, 14> jump_bytes(void* target) {
        std::array<uint8_t, 14> stub{0xff, 0x25, 0x00, 0x00, 0x00, 0x00};  // jmp qword ptr [rip+0]
        auto address = reinterpret_cast<uintptr_t>(target);
        std::memcpy(stub.data() + 6, &address, sizeof(address));
        return stub;
    }

    uint8_t* next_;
    uint8_t* end_;
};

// Points the rel32 operand at `operand`, whose instruction ends at `instruction_end`, to `target`.
bool retarget(uintptr_t operand, uintptr_t instruction_end, const void* target) {
    int64_t displacement = reinterpret_cast<int64_t>(target) - reinterpret_cast<int64_t>(game::base() + instruction_end);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        return false;
    }
    auto relative = static_cast<int32_t>(displacement);
    return patch(game::base() + operand, &relative, sizeof(relative), PAGE_EXECUTE_READWRITE);
}

bool redirect_call(uintptr_t site, uint8_t* stub) {
    return stub && retarget(site + 1, site + 5, stub);
}

// An empty slot gets one entry, "Start Randovania Seed": its first entry gets our text key and `slot_newrandom_`,
// and the "Randomize mode" submenu after it is jumped over to the "Back" entry.
std::string patch_menu(CodeCave& cave) {
    const game::Build& build = game::build();
    std::string_view key = new_game::kMenuTextKey;
    uint8_t* text = cave.place(std::span(reinterpret_cast<const uint8_t*>(key.data()), key.size() + 1));
    if (!text || !retarget(build.menu_start_text + 3, build.menu_start_text + 7, text)) {
        return "cannot point the start entry at its text";
    }
    // Without a seed the entry shows why and gets a command nothing handles.
    new_game::MenuEntry entry = new_game::prepare_menu();
    const uint8_t* command = game::base() + build.menu_newrandom_string;
    if (!entry.startable) {
        std::string_view inert = new_game::kNoSeedCommand;
        command = cave.place(std::span(reinterpret_cast<const uint8_t*>(inert.data()), inert.size() + 1));
    }
    if (!command || !retarget(build.menu_start_command + 3, build.menu_start_command + 7, command)) {
        return "cannot point the start entry at its command";
    }
    // The code after the submenu expects r12 == 0: xor r12d, r12d.
    uint8_t* skip = cave.run_then_jump({0x45, 0x31, 0xe4}, game::base() + build.menu_skip_to);
    if (!skip) {
        return "cannot write to the code cave";
    }
    // jmp rel32 to that stub over the 7-byte mov at menu_skip_from, plus two nops.
    std::array<uint8_t, 7> jump{0xe9, 0, 0, 0, 0, 0x90, 0x90};
    if (!patch(game::base() + build.menu_skip_from, jump.data(), jump.size(), PAGE_EXECUTE_READWRITE) ||
        !retarget(build.menu_skip_from + 1, build.menu_skip_from + 5, skip)) {
        return "cannot jump over the Randomize mode submenu";
    }
    return {};
}

}  // namespace

std::string install() {
    uint8_t* base = game::base();
    g_tick = reinterpret_cast<GameFn>(base + game::build().tick);
    g_pickup_dialog = reinterpret_cast<GameFn>(base + game::build().pickup_dialog);
    g_randomizer_loader = reinterpret_cast<GameFn>(base + game::build().randomizer_loader);
    g_dialog_init = reinterpret_cast<GameFn>(base + game::build().dialog_init);
    g_beacon_use = reinterpret_cast<GameFn>(base + game::build().beacon_use);

    // First, so as few files as possible are opened before it.
    g_fopen = reinterpret_cast<FopenFn>(hook_import("fopen", reinterpret_cast<void*>(&hook_fopen)));
    g_fopen_s = reinterpret_cast<FopenSFn>(hook_import("fopen_s", reinterpret_cast<void*>(&hook_fopen_s)));
    if (!g_fopen || !g_fopen_s) {
        return "cannot hook the game's fopen and fopen_s imports";
    }

    CodeCave cave;
    if (!redirect_call(game::build().randomizer_loader_call, cave.jump_to(&hook_randomizer_loader))) {
        return std::format("cannot redirect the call at {:#x}", game::build().randomizer_loader_call);
    }
    // The original sprintf, read before the call is redirected.
    uintptr_t label_call = game::build().slot_label_format_call;
    int32_t label_rel = 0;
    std::memcpy(&label_rel, base + label_call + 1, sizeof(label_rel));
    g_slot_label_format = reinterpret_cast<GameFn>(base + label_call + 5 + label_rel);
    if (!redirect_call(game::build().slot_label_format_call, cave.jump_to(&hook_slot_label_format))) {
        return std::format("cannot redirect the call at {:#x}", game::build().slot_label_format_call);
    }
    if (!redirect_call(game::build().pickup_dialog_init_call, cave.jump_to(&hook_pickup_dialog_init))) {
        return std::format("cannot redirect the call at {:#x}", game::build().pickup_dialog_init_call);
    }
    if (std::string error = patch_menu(cave); !error.empty()) {
        return error;
    }
    // Every scripted object's state gets the wrapped unlockAchievement from here on.
    auto natives = game::find_natives();
    auto unlock = std::ranges::find(natives, std::string("unlockAchievement"), &game::Native::name);
    if (unlock == natives.end()) {
        return "the game registers no unlockAchievement native";
    }
    auto wrapped = bridge::wrap_unlock_achievement(reinterpret_cast<bridge::NativeFn>(base + unlock->rva));
    if (!retarget(unlock->registration + 3, unlock->registration + 7, cave.jump_to(reinterpret_cast<void*>(wrapped)))) {
        return std::format("cannot point the unlockAchievement registration at {:#x} to the DLL", unlock->registration);
    }

    auto tick = reinterpret_cast<uintptr_t>(&hook_tick);
    for (uintptr_t slot : game::build().tick_vtable_slots) {
        if (!patch(base + slot, &tick, sizeof(tick), PAGE_READWRITE)) {
            return std::format("cannot patch the vtable slot at {:#x}", slot);
        }
    }
    auto beacon_use = reinterpret_cast<uintptr_t>(&hook_beacon_use);
    if (!patch(base + game::build().beacon_use_vtable_slot, &beacon_use, sizeof(beacon_use), PAGE_READWRITE)) {
        return std::format("cannot patch the vtable slot at {:#x}", game::build().beacon_use_vtable_slot);
    }
    auto pickup_dialog = reinterpret_cast<uintptr_t>(&hook_pickup_dialog);
    if (!patch(base + game::build().pickup_dialog_vtable_slot, &pickup_dialog, sizeof(pickup_dialog), PAGE_READWRITE)) {
        return std::format("cannot patch the vtable slot at {:#x}", game::build().pickup_dialog_vtable_slot);
    }
    return {};
}

}  // namespace oyr::hooks
