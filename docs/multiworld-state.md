# Where the multiworld state lives

For multiworld, Randovania has to know **which** location the player collected, not just how many. This is where
each of the four state packets gets its answer, and how each survives a save, a quit and continue, and a death.
RVAs are Epic's; the other builds are in `native/dll/builds.hpp`, and the wider analysis is in
[exe-research.md](exe-research.md).

## Packet 6, collected locations

The collected set is **the game's own randomizer entry vector** at `globals + 0x35d8`; the DLL only reads it. The
inventory cannot answer this, because every Nothing stacks in one slot (see the last section).

`std::vector<RandomizerEntry>`, element size **0x68**:

| Offset | Field | Save key |
|---|---|---|
| +0x00 | `int32 id`: `level number << 16 \| object id` | `id` |
| +0x28 | `std::string item`, the placed item | `item` |
| +0x48, +0x4c | `int32 tracker_x`, `int32 tracker_y` | `tracker` |
| +0x50 | `bool revealed` | `revealed` |
| +0x51 | **`bool collected`** | `collected` |

The layout is YokuArchipelagoMod's `RandomizerItem`, confirmed in the exe:

- `save_load` (0x26b0c0) passes `globals + 0x35d8` to the `"randomizer"` serializer (0x16bfd0), which reads and
  writes exactly these offsets. The vector **is** `_global.randomizer`, in the same order.
- `bubble_pickup` (0x164da0) calls `mark_collected` (0x16b040) on `globals + 0x3550`; it finds the entry by `id` in
  `[this+0x88, this+0x90)` and sets `+0x51`. `0x3550 + 0x88 = 0x35d8`.
- On level load (0x165ed0) the game hides a pickup whose entry is `revealed` or `collected`.

| Event | What happens |
|---|---|
| Collect a pickup | `collected` is set at once; the next poll sends it. |
| Save / quit and continue | `save_load` writes and reads the whole vector; packet 6 resends the full set. |
| Death | The ball respawns without a reload; the flag stays, as the pickup does not come back. |
| Quit without saving | The game forgets the pickup, but it was already reported. Same as every Randovania multiworld game. |

Payload `"locations:"` plus one bit per Randovania location index, least significant bit of each byte first, as
Randovania's connector reads it. Always the full set, sent only while a save is loaded
and only when it changed.

**Mapping.** Randovania sends the order at handshake: `RL.SetLocations("<id>,<id>,…")` lists the location ids in
`PickupIndex` order, and bit *n* is the entry whose `id` is the *n*-th value. Nothing is sent before that. The
vector's own order is not used, so Randovania's node order never depends on the patcher's CSV row order.

## Packet 5, inventory

The `std::vector<InventoryItem>` at `globals + 0x29e8`, element size 0x60, item id at `+0x08`, count at `+0x38` (the
lookup `getItemCount` does). Payload `{"index": N,"inventory":[…]}`, counts in the order Randovania sent with
`RL.SetInventoryItems`. The inventory is save data, so it needs nothing extra.

`index` goes up whenever one of these counts changes: a local pickup, a remote one, or loading another save. It lives
in the DLL's memory only and starts at 0 with the game. Randovania names a remote pickup (its stage, its dialog
line) from the inventory it last saw and passes that `index` to `RL.GiveItem`. The tick counts changes before it
gives queued pickups, so a pickup named from an older inventory is dropped. The DLL then sends packets 5 to 7 again,
even unchanged, and packet 7 ends Randovania's cooldown, so it sends the pickup again from the current inventory.

## Packet 7, received pickups

This must persist, or a continued save would get every item again. The count lives in **`quests_completed`**
(`std::vector<std::string>` at `globals + 0x2688`) as one entry `@open-yoku-rando:received:<n>`. The game keeps
entries it does not know, so the count is saved with no save hook.

- `multiworld::received_count()` reads it.
- `multiworld::note_pickup_received()` rewrites it on the tick the item is actually given, never earlier.
- Quitting without saving reverts the item and the count together, and Randovania sends it again.

## Packet 8, game state

Payload `"<region>;<beaten>;<level>"`, or `"MAINMENU;false"` when the level is empty or `_start_screen`. `<region>`
is the level name up to the first `_` (Intro, Hub, Jungle, Spring, Peak, Cave), which the connector matches against
`region.extra["scenario_id"]`; the full level name follows for later use. `<beaten>` is true once the final boss is
beaten: when the game unlocks the `end_1` achievement, the DLL adds `@open-yoku-rando:victory` to `quests_completed`.

## Which save

A new game started from `seed.txt` gets `@open-yoku-rando:uuid:<layout uuid>` in `quests_completed`, next to the
identifier. Randovania connects only while such a save is loaded: it reads `RL.LayoutUUID()` to pick the layout,
then sends `RL.SetIdentifier("uuid:<layout uuid>")`. The DLL then reports nothing from, and writes nothing into, a
save without that entry, so loading another seed's save stops the reports until Randovania reconnects.

## Polling

The DLL pushes. Every 30 ticks (about twice a second) it builds each payload and sends it only if it changed. On a
client reconnect and on returning to the main menu it forgets what it sent, so everything is reported again. The
order is 8, 5, 6, 7, because Randovania only accepts a received count once it knows the region.

## Nothing, and why it keeps its inventory slot

Every pickup the game has no item of its own for is `nothing`: an empty location and one holding another player's
item are the same pickup. What makes it a multiworld pickup is the location, which the DLL reports and whose dialog
line names the real item and player. There is no separate `offworld` id.

A pickup can choose its bubble model: the patcher's `model` mints `nothing_<model>`, since the game derives the model
path from the id. The DLL's `pickup_dialog` hook collapses it back to `nothing` before the game's `inventory_add` and
dialog lookup, so all Nothings stack in one slot and the player keeps a visible count of them.

If the slot is ever unwanted: `pickup_dialog` (0x1f4110) skips its `inventory_add` when the byte at pickup object
`+0x280` is set (`cmp byte [rcx+0x280], 0; jne` at 0x1f4188). Setting it in the hook would collect the location
without an inventory entry. The DLL does not use it, and `verify()` does not check it.
