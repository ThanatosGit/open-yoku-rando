#include "game.hpp"
#include "log.hpp"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <format>

namespace oyr::game {
namespace {

uint8_t* g_base = nullptr;
const Build* g_build = nullptr;

struct Prologue {
    const char* name;
    uintptr_t Build::* rva;
    std::array<int16_t, 12> bytes;  // -1: not pinned (rip-relative operand)
};

// First bytes of every game function the DLL calls or hooks; the same in all builds.
constexpr std::array<Prologue, 21> kPrologues{{
    {"randomizer_loader", &Build::randomizer_loader, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55}},
    {"inventory_add", &Build::inventory_add, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55}},
    {"inventory_remove", &Build::inventory_remove, {0x40, 0x55, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0x40, 0x07, 0x00, 0x00}},
    {"pickup_dialog", &Build::pickup_dialog, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x55}},
    {"dialog_init", &Build::dialog_init, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x55, 0x56, 0x57, 0x41, 0x56}},
    {"dialog_line", &Build::dialog_line, {0x40, 0x56, 0x57, 0x41, 0x57, 0x48, 0x81, 0xec, 0x70, 0x03, 0x00, 0x00}},
    {"tick", &Build::tick, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89}},
    {"script_api_init", &Build::script_api_init, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55}},
    {"run_script", &Build::run_script, {0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b}},
    {"lua_newstate", &Build::lua_newstate, {0x40, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x50, 0x48, 0x8b, 0x05, -1}},
    {"lua_alloc", &Build::lua_alloc, {0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0xc2, 0x48, 0x8b, 0xca, 0x4d, 0x85}},
    {"luaL_openlibs", &Build::luaL_openlibs, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56}},
    {"lua_setglobal", &Build::lua_setglobal, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48}},
    {"lua_pushstring", &Build::lua_pushstring, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b}},
    {"save_load", &Build::save_load, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48}},
    {"bubble_pickup", &Build::bubble_pickup, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54}},
    {"loot_table_find", &Build::loot_table_find, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89}},
    {"loot_spawn", &Build::loot_spawn, {0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89}},
    {"mark_collected", &Build::mark_collected, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89}},
    {"item_rule_action", &Build::item_rule_action, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55}},
    {"beacon_use", &Build::beacon_use, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48}},
}};

// The instructions the struct offsets were read from (docs/multiworld-state.md), so a build with another layout is
// rejected instead of misread. -1 marks rel32 operands.
struct Anchor {
    const char* what;
    uintptr_t Build::* rva;
    std::array<int16_t, 14> bytes;
};
constexpr std::array<Anchor, 6> kAnchors{{
    // save_load: lea r8, [rbx + 0x35d8]; mov rcx, rsi; call randomizer_serialize (rbx = globals)
    {"the randomizer entry vector at globals+0x35d8",
     &Build::anchor_randomizer_entries,
     {0x4c, 0x8d, 0x83, 0xd8, 0x35, 0x00, 0x00, 0x48, 0x8b, 0xce, 0xe8, -1, -1, -1}},
    // randomizer_serialize writes "collected" from lea r8, [rbx + 0x51]
    {"the collected flag at entry+0x51", &Build::anchor_collected_flag, {0x4c, 0x8d, 0x43, 0x51, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // mark_collected: add r9, 0x68
    {"the entry stride of 0x68", &Build::anchor_entry_stride, {0x49, 0x83, 0xc1, 0x68, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // mark_collected: mov byte [r13 + 0x51], 1
    {"the collected store in mark_collected", &Build::anchor_collected_store, {0x41, 0xc6, 0x45, 0x51, 0x01, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    // bubble_pickup: mov rcx, [rip + globals]; add rcx, 0x3550; the state's entries at +0x88 are globals+0x35d8.
    // Both rel32 operands are checked in verify().
    {"the randomizer state at globals+0x3550", &Build::anchor_randomizer_state, {0x48, 0x8b, 0x0d, -1, -1, -1, -1, 0x48, 0x81, 0xc1, 0x50, 0x35, 0x00, 0x00}},
    // mark_collected: mov rbx, [r12 + 0x38]; mov r15, [r12 + 0x40] - the onpickup rule vector of the state
    {"the item rules at state+0x38", &Build::anchor_item_rules, {0x49, 0x8b, 0x5c, 0x24, 0x38, 0x4d, 0x8b, 0x7c, 0x24, 0x40, -1, -1, -1, -1}},
}};
// A size larger than the list would leave a zero entry that checks the DOS header instead.
static_assert(std::ranges::none_of(kAnchors, [](const Anchor& anchor) { return anchor.what == nullptr; }),
              "kAnchors declares more entries than it initialises");

// mov rcx, [rip + globals]; add rcx, 0x8fd0 - how the menu builder starts every entry.
constexpr std::array<int16_t, 14> kMenuEntryStart{0x48, 0x8b, 0x0d, -1, -1, -1, -1, 0x48, 0x81, 0xc1, 0xd0, 0x8f, 0x00, 0x00};

// mov r9d, [r14 + 0xc]; lea rdx, [rip + fmt] - the seed and the format before the slot label's sprintf.
constexpr std::array<int16_t, 7> kSlotLabelSeed{0x45, 0x8b, 0x4e, 0x0c, 0x48, 0x8d, 0x15};

// One native registration in script_api_init:
// lea rdx,[fn]; mov [rax],rdx; mov dword [rax+8],LUA_TLCF; add qword [rcx+0x10],0x10;
// lea rdx,[name]; mov rcx,[r13]; call lua_setglobal
constexpr std::array<int16_t, 38> kRegistration{
    0x48, 0x8d, 0x15, -1,   -1,   -1,   -1, 0x48, 0x89, 0x10, 0xc7, 0x40, 0x08, 0x16, 0x00, 0x00, 0x00, 0x48, 0x83,
    0x41, 0x10, 0x10, 0x48, 0x8d, 0x15, -1, -1,   -1,   -1,   0x49, 0x8b, 0x4d, 0x00, 0xe8, -1,   -1,   -1,   -1,
};

template <size_t N>
bool matches(uintptr_t rva, const std::array<int16_t, N>& bytes) {
    for (size_t i = 0; i < N; ++i) {
        if (bytes[i] >= 0 && g_base[rva + i] != bytes[i]) {
            return false;
        }
    }
    return true;
}

template <class T>
T read(uintptr_t rva) {
    T value;
    std::memcpy(&value, g_base + rva, sizeof(T));
    return value;
}

// Target RVA of the `call rel32` at site, or 0 if there is none.
uintptr_t call_target(uintptr_t site) {
    if (g_base[site] != 0xe8) {
        return 0;
    }
    return site + 5 + read<int32_t>(site + 1);
}

const Build* find_build() {
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
    for (const Build* build : kBuilds) {
        if (build->timestamp == nt->FileHeader.TimeDateStamp) {
            return build;
        }
    }
    return nullptr;
}

std::string check_code(const Build& b) {
    if (g_base[check_byte] != b.check_byte_value) {
        return std::format("{} build: byte at {:#x} is {:#04x} instead of {:#04x}", b.name, check_byte, g_base[check_byte], b.check_byte_value);
    }
    for (const Prologue& prologue : kPrologues) {
        if (!matches(b.*prologue.rva, prologue.bytes)) {
            return std::format("{} build: {} at {:#x} has unexpected bytes", b.name, prologue.name, b.*prologue.rva);
        }
    }
    for (const Anchor& anchor : kAnchors) {
        if (!matches(b.*anchor.rva, anchor.bytes)) {
            return std::format("{} build: the code at {:#x} that proves {} has unexpected bytes", b.name, b.*anchor.rva, anchor.what);
        }
    }
    uintptr_t state = b.anchor_randomizer_state;
    if (state + 7 + read<int32_t>(state + 3) != b.globals_ptr || call_target(state + 0x14) != b.mark_collected) {
        return std::format("{} build: bubble_pickup does not reach mark_collected through globals+0x3550", b.name);
    }

    for (uintptr_t slot : b.tick_vtable_slots) {
        if (read<uintptr_t>(slot) != reinterpret_cast<uintptr_t>(g_base) + b.tick) {
            return std::format("{} build: vtable slot {:#x} does not point to tick", b.name, slot);
        }
    }
    if (read<uintptr_t>(b.pickup_dialog_vtable_slot) != reinterpret_cast<uintptr_t>(g_base) + b.pickup_dialog) {
        return std::format("{} build: vtable slot {:#x} does not point to pickup_dialog", b.name, b.pickup_dialog_vtable_slot);
    }
    if (read<uintptr_t>(b.beacon_use_vtable_slot) != reinterpret_cast<uintptr_t>(g_base) + b.beacon_use) {
        return std::format("{} build: vtable slot {:#x} does not point to beacon_use", b.name, b.beacon_use_vtable_slot);
    }
    if (call_target(b.pickup_dialog_init_call) != b.dialog_init) {
        return std::format("{} build: no call to dialog_init at {:#x}", b.name, b.pickup_dialog_init_call);
    }
    if (!matches(b.slot_label_format_call - 0x14, kSlotLabelSeed) ||
        std::strcmp(reinterpret_cast<const char*>(g_base + b.slot_label_format_call - 9 + read<int32_t>(b.slot_label_format_call - 0xd)), " (%s: %i)") != 0 ||
        call_target(b.slot_label_format_call) == 0) {
        return std::format("{} build: the slot label code at {:#x} is not what the DLL patches", b.name, b.slot_label_format_call);
    }
    if (call_target(b.randomizer_loader_call) != b.randomizer_loader) {
        return std::format("{} build: no call to the randomizer loader at {:#x}", b.name, b.randomizer_loader_call);
    }
    // The menu patch rewrites two rip-relative string operands and jumps over a block.
    auto lea_string = [](uintptr_t at, uint8_t modrm, const char* text) {
        if (g_base[at] != 0x4c || g_base[at + 1] != 0x8d || g_base[at + 2] != modrm) {
            return false;
        }
        return std::strcmp(reinterpret_cast<const char*>(g_base + at + 7 + read<int32_t>(at + 3)), text) == 0;
    };
    auto menu_entry_start = [&b](uintptr_t at) { return matches(at, kMenuEntryStart) && at + 7 + read<int32_t>(at + 3) == b.globals_ptr; };
    if (!lea_string(b.menu_start_text, 0x0d, "ui_start_game") || !lea_string(b.menu_start_command, 0x05, "slot_new_") || !menu_entry_start(b.menu_skip_from) ||
        !menu_entry_start(b.menu_skip_to) || !lea_string(b.menu_skip_to + 0xe, 0x0d, "ui_back") ||
        std::strcmp(reinterpret_cast<const char*>(g_base + b.menu_newrandom_string), "slot_newrandom_") != 0) {
        return std::format("{} build: the slot menu code is not what the DLL patches", b.name);
    }
    for (uintptr_t address = b.code_cave; address < b.code_cave_end; ++address) {
        if (g_base[address] != 0) {
            return std::format("{} build: code cave at {:#x} is in use; is another mod loaded?", b.name, address);
        }
    }
    // find_natives reads through build().
    g_build = &b;
    if (find_natives().empty()) {
        g_build = nullptr;
        return std::format("{} build: no script natives found in script_api_init", b.name);
    }
    return {};
}

}  // namespace

std::string verify() {
    g_base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));

    const Build* found = find_build();
    if (!found) {
        return "this Yoku.exe is none of the supported builds (Epic, GOG and Steam, all from 2021-09-26)";
    }
    // Steam's DRM decrypts .text on the main thread, front to back, while the DLL starts on its own thread, so one
    // decrypted byte says nothing about code further up. Retry every check until the whole code is in place.
    std::string error;
    for (int waited = 0; waited < 10000; waited += 50) {
        error = check_code(*found);
        if (error.empty()) {
            log::info("{} build of Yoku.exe", found->name);
            return {};
        }
        Sleep(50);
    }
    return error;
}

const Build& build() {
    return *g_build;
}

uint8_t* base() {
    return g_base;
}

std::vector<Native> find_natives() {
    std::vector<Native> natives;
    for (uintptr_t at = build().script_api_init; at + kRegistration.size() <= build().script_api_init_end; ++at) {
        if (!matches(at, kRegistration) || at + 38 + read<int32_t>(at + 34) != build().lua_setglobal) {
            continue;
        }
        uintptr_t function = at + 7 + read<int32_t>(at + 3);
        uintptr_t name = at + 29 + read<int32_t>(at + 25);
        natives.push_back({reinterpret_cast<const char*>(g_base + name), function, at});
    }
    return natives;
}

}  // namespace oyr::game
