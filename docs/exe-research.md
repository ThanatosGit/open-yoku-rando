# What the exe gives us

The facts about `Yoku.exe` that `native/dll/builds.hpp` and the DLL rest on. From static analysis, in-game tests, and
[YokuArchipelagoMod](https://git.makuluni.com/Archipelago/YokuArchipelagoMod), whose addresses were re-verified here.
Where each multiworld packet gets its contents is in [multiworld-state.md](multiworld-state.md).

## The builds we support

Three builds of `Yoku.exe`, all compiled 2021-09-26 from the same code:

| Build | Size | PE timestamp | Byte at 0x203154 | Game code vs. Epic | Lua library vs. Epic |
|---|---|---|---|---|---|
| Epic | 7,427,584 | 0x615036bf | 0x48 | — | — |
| GOG | 7,423,488 | 0x61503c9f | 0x2b | −0x750 | same |
| Steam | 7,664,312 | 0x61502fec | 0x07 (decrypted) | +0x700 / +0x6c0 | −0x60 (`run_script`, `lua_alloc` same) |

All RVAs below are **Epic**'s; `builds.hpp` has one `Build` table per build. The DLL picks the table by PE timestamp,
then checks the byte at 0x203154, the first bytes of every function used, the anchors, the hooked vtable slots, the
redirected call site and the code cave. If anything differs it hooks nothing and the game runs unmodified. Steam's
`.text` is encrypted on disk (SteamStub), so the DLL waits up to ten seconds for the check byte to appear before
checking code.

**Getting into the game.** All builds import `XInputGetState` from `xinput9_1_0.dll`, which the game does not ship and
which is no KnownDLL, so Windows loads it from the game folder. `native/proxy/xinput_proxy.cpp` is that DLL: it
forwards the one function to the system's copy and starts `open_yoku_rando.dll`.

## Lua inside the game

- Lua **5.2**, statically linked. `luaL_openlibs` opens no io, os, package or require.
- Every scripted object has **its own `lua_State`**, created by `script_api_init` with 84 natives, also on
  level-loading threads. A state dies with its object, so the DLL makes its own on the game thread.
- `run_script` takes a context whose first field is the `lua_State*`; callers set `[L+0xd0]`, Yoku's "object whose
  script is running" field.
- `addItem` is not a plain inventory add: it spawns the placed item of the current object's location, so it is unsafe
  without a current object. Safe natives: `getFruit`, `getItemCount`, `hasItem`, `getGameMode`, `addFruit`.
- **In a level:** the level name at `globals+0x25b8` is neither empty nor `_start_screen` (the main menu is a level of
  its own). Item delivery waits for that, a running frame and the dialog flag at `globals+0x4dd0` being clear.

## Text at runtime

`globals+0x9018` (UI) and `globals+0x9028` (dialog) point at a text file object whose `+0x78` is a
`std::vector<std::array<std::string, 2>>` of key/value pairs. Adding **new** keys there works; that is how the DLL adds
its own texts and dialogs. Changing a key the game ships does not, because the loaded translation wins.

## The pickup text hook

`pickup_dialog` (0x1f4110, vtable slot 0x4b22e8) calls `inventory_add` and then, only if `items/<id>_1` exists, shows
the dialog `items/<id>`. The DLL hooks it to copy the location's line over `items/nothing_1` before the game reads it,
and to collapse `nothing_<model>` back to `nothing` so per-pickup models share one inventory slot. The location is

```
([[pickup_item + 0x80] + 0x6e0] + 0x20) << 16 | [pickup_item + 0x20]
```

the same `level << 16 | object id` that keys the randomizer entries. There is no data-only route to this: no native
sets text, `pickup_item` has no script hook on collect, and no `randomizer_scripts.csv` row fires here.

## Starting a randomizer game

The randomizer loader (0x168da0) runs on **every** level load in game mode 3. It reads `randomizer_scripts.csv`, then
fills the entry vector from the difficulty's CSV (ids, vanilla items, trackers, spawn flags). Only if the vector was
empty before, i.e. a new game, does it roll: a permutation of the CSV's own item column.

The DLL patches the slot menu so an empty slot offers only "Start Randovania Seed" (`slot_newrandom_<n>`), and wraps
the loader call: a new game gets one placeholder entry first, so the loader takes the loaded-save path and never
rolls. Afterwards the DLL writes every entry's item, the starting items, fruit and the identifier from
`<game>/open-yoku-rando/seed.txt`. Nothing reads the entries earlier, because a pickup's level number is set right
before the loader runs.

## Beacons and the ending

The 8 beacons (`special_collectible_beacon`) are lit by `beacon_use` (0x216450, vtable slot 0x4b5f50): with fewer
than 10 `collectible` (Wickerlings) in the inventory it does nothing, otherwise it removes 10, sets `+0x270` and adds
the beacon to its map area's `beacons_activated` (`+0x408`). All 8 hatch the egg in `cave_temple_terror_upper`, the
second ending. The DLL hooks the slot and, when the Wickerling count dropped, adds
`@open-yoku-rando:beacon:<n>` to `quests_completed`; the patcher's override of Nim's `instruments_2` checks
`hasCompletedQuest` for the required one before the ceremony that leads to the final boss.

After the final boss, Nim's `cutscene_win` calls `unlockAchievement("end_1")` before the credits. No quest is
completed after the boss, and `_global.achievments_awarded` is not reliable in a randomizer save, so the DLL points
the `unlockAchievement` registration in `script_api_init` at a wrapper that adds `@open-yoku-rando:victory`.

## Addresses (Epic; GOG and Steam in `builds.hpp`)

| What | RVA | Notes |
|---|---|---|
| build check byte | 0x203154 | Epic 0x48 |
| globals pointer | 0x5455e8 | |
| code cave | 0x41231f–0x413000 | zero padding at the end of `.text`, for jump stubs |
| `inventory_add` | 0x296bc0 | `void*(Inventory*, const char* item)` |
| `inventory_remove` | 0x297210 | |
| `dialog_init` / `dialog_line` | 0x28b7a0 / 0x28be70 | string table `globals+0x4de0` |
| `pickup_dialog` | 0x1f4110 | vtable slot 0x4b22e8 |
| `beacon_use` | 0x216450 | vtable slot 0x4b5f50 |
| `spawn_bubble` | 0x1f3560 | loads `processed/items/<id>_x102.sim` |
| `bubble_pickup` / `mark_collected` | 0x164da0 / 0x16b040 | sets `collected` at entry +0x51 |
| `create_object` | 0x157610 | `shared_ptr*(out, type, Level*)`: a new object with its defaults; `guide_sign_popup` is type 0x45 |
| `popup_show` / `popup_hide` | 0x247c10 / 0x247cf0 | the manager at globals+0x2f50 shows one popup at a time, keeps raw pointers and draws it through vtable slot 25 (0x248316) |
| `b2World::CreateBody` | 0x68180 | null while `m_flags` (+0x19298) has bit 1 (locked, inside a step); a level's world is at +0x2c0 |
| `loot_table_find` | 0x166620 | `LootTable*(globals+0x89f8, name)`, null for a non-loot item |
| `loot_spawn` | 0x163040 | loot bubble at a position; owner = the spawner's weak self pointer (+0x8/+0x10); with an owner that hands it out at once, `bubble_pickup(bubble, ball)` follows at 0x162e20 |
| `save_load` | 0x26b0c0 | serializes the globals |
| `tick` | 0x25c280 | vtable slots 0x4af8b8, 0x4bbf18, 0x4bdab0 |
| `script_api_init` | 0x205020 | registers the 84 natives |
| `run_script` | 0x293f0 | `bool(context, source)` |
| `lua_newstate` | 0xe4a40 | |
| game Lua allocator | 0x29820 | |
| `luaL_openlibs` | 0xd9880 | |
| `lua_setglobal` / `lua_pushstring` | 0xcb180 / 0xcae70 | |
| randomizer loader | 0x168da0 | `(RandomizerState*, level data, bool dump)` |
| slot menu builder | 0x2add70 | |

Globals offsets: level name 0x25b8, frame counter 0x2028, inventory 0x29e8, fruit 0x2a30 / total 0x2a34, wallet size
0x2a10, ability flags 0x2a00, quests completed 0x2688, seed 0x35d0, difficulty 0x35d4, randomizer entries 0x35d8,
dialog flag 0x4dd0, string table 0x4de0, UI / dialog text 0x9018 / 0x9028. World 0x1f80 (World* -> ball +0x830; ball -> physics body
+0x18 -> float position +0xc/+0x10 in metres, times 100 for level units: read at 0x1791c8, the factor loaded at
0x17906a), loot tables 0x89f8.

**Randomizer entry:** stride 0x68; `id` +0x00, vanilla item +0x08 (from the CSV, not saved), placed item +0x28,
tracker +0x48/+0x4c, `revealed` +0x50, `collected` +0x51. The vector at `globals+0x35d8` is the save's
`_global.randomizer`, in the same order.

## Porting to other builds

- **The natives** are registered by a fixed 38-byte sequence in `script_api_init` (`lea rdx,[fn]`, `mov [rax],rdx`,
  `mov dword [rax+8],0x16`, `add qword [rcx+0x10],0x10`, `lea rdx,[name]`, `mov rcx,[r13]`, `call lua_setglobal`;
  rel32 at +3, +25, +34). Scanning for it finds all 84 with their names.
- **String anchors:** `lua_debug> ` and `=(debug command)` lead to `lua_load`; `Can't load
  data/text/randomizer_scripts.csv` leads to the randomizer loader.
- **Byte patterns:** each Epic function with its rip-relative and rel32 operands masked is found exactly once in the
  other builds' `.text`; that is how GOG and Steam were mapped.
