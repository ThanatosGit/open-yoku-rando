# The DLL's socket and Lua interface

`open_yoku_rando.dll` listens on `127.0.0.1:6970` (or `OPEN_YOKU_RANDO_PORT`) and runs Lua sent to it on the game
thread, speaking Randovania's socket protocol. For the Randovania side and for debugging; players never see it.

## Lua

Remote code runs in the DLL's own Lua 5.2 state, which keeps its globals for the session. It has the libraries the
game opens (no io, os or package) and only the natives that touch global state: `getFruit`, `addFruit`,
`getItemCount`, `hasItem`, `getGameMode`. Natives that act on a running script object, like `addItem`, are not
available; use `RL.GiveItem`.

| Function | |
|---|---|
| `RL.Version` | `1` |
| `RL.BufferSize` | Largest Lua chunk per packet, `65536`. |
| `RL.GiveItem(item, text [, received, inventory])` | Gives `item` once the game is in a level, running and no dialog is open, then shows `text` as a message if it is not empty. `item` may be a progression, see below. With `received` and `inventory`, the item is only given if the save matches the identifier, has received exactly `received` remote pickups and packet 5's `index` is still `inventory`; otherwise it is dropped and packets 5 to 7 are sent again. |
| `RL.ShowMessage(text)` | Shows `text` as a message, see below. |
| `RL.RemoveItem(item [, amount])` | Removes `amount` copies, or all; same timing, no dialog. Safe when the item is not held. |
| `RL.SetText(key, value)` | Adds a text key (`ui_*` to the UI table, others to dialog). Keys the game ships cannot be changed. |
| `RL.Log(message)` | Writes to the log and to clients that asked for logging. |
| `RL.Level()` | Current level, `_start_screen` in the main menu. |
| `RL.InGame()` | True in a level. |
| `RL.Running()` | True while the game is simulating, not paused or loading. |
| `RL.SetLocations(ids)` | Arms multiworld: location ids in `PickupIndex` order; bit *n* of packet 6 is the *n*-th id. Returns the count accepted. |
| `RL.SetInventoryItems(items)` | Item ids in the order packet 5 counts them. |
| `RL.SetIdentifier(id)` | Only a save with the matching `@open-yoku-rando:<id>` entry is reported or written to. |
| `RL.Received()` | Remote pickups this save has been given; kept in the save. |
| `RL.LayoutUUID()` | The layout UUID the loaded save was started with, empty in the main menu or for a save without one. |

**Messages.** Drawn on top of the game, centred near the top, by a Dear ImGui overlay hooked into the swap chain's
`Present`. Up to five stand below each other, each for about five seconds of running game, so a pause or a level load
keeps them up. A given item waits 1.5 seconds after the message before it, so remote items do not arrive all at once.

**Fruit.** An item that names a loot table (`reward_fruit_big`, `reward_fruit_medium`) is not added to the
inventory: the DLL spawns that loot as a bubble at the ball and lets the ball take it at once, the way the game hands
out a fruit location, so the game's own fruit rules apply.

**Progressions.** For a progressive pickup, pass every stage from the lowest up, separated by commas, e.g.
`RL.GiveItem("abilities/slug_vaccum,abilities/slug_upgrade", ...)`. The DLL decides when it gives the item: the stage
above the highest one held, or the last stage again once all are held. A stage it gives replaces the one below it,
which is removed, as the game does for its own upgrade pickups. A single item id is a progression of one stage.

A chunk's first return value comes back as a string (`nil` as empty); errors as a failed reply. Requests are only run
while the game ticks; one sent during a level load or on the map, pause or dialog screen waits until the game ticks
again. A disconnect drops the requests not yet run.

## Protocol

TCP on `127.0.0.1`, one client at a time. Every packet starts with its type byte; lengths are little-endian u32.

| Type | Client → game | Game → client |
|---|---|---|
| 1 handshake | `01 <interests>` (1 = logging, 2 = multiworld) | `01 <request number>` |
| 2 log | | `02 <length> <utf-8>` |
| 3 Lua | `03 <length> <source>` | `03 <request number> <success> <length> <result>` |
| 4 keep-alive | `04` | |
| 5 inventory | | `05 <length> {"index": n,"inventory":[…]}` |
| 6 collected locations | | `06 <length> locations:<bitfield>` |
| 7 received pickups | | `07 <length> <count>` |
| 8 game state | | `08 <length> <region>;<beaten>;<level>` |

The request number counts replies per connection, from 0. Packets 5 to 8 need the multiworld interest and
`RL.SetLocations`; the game pushes them about twice a second when they change. Their contents are explained in
[multiworld-state.md](multiworld-state.md), the DLL's hooks in [exe-research.md](exe-research.md).
