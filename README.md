# open-yoku-rando

Open source randomizer for Yoku's Island Express, made for [Randovania](https://github.com/randovania/randovania).
Randovania generates the seed; this project puts it into the game:

- **the patcher** (Python, `src/open_yoku_rando`) writes the seed into the game folder;
- **the DLL** (C++, `native/`) loads with the game, adds "Start Randovania Seed" to the menu, applies the seed, shows
  each location's text at pickup and talks to Randovania for multiworld.

Supports all 248 locations, starting items and fruit, Nothing pickups (empty spots and other players' items),
per-location texts, a number of lit beacons required before the final boss, multiworld, and the Epic, GOG and Steam
builds of the game. No file of the game is changed.

Documentation:
- [How it works](docs/how-it-works.md) — from Randovania's export to the started seed
- [Development](docs/development.md) — patcher usage, tests, building the DLL
- [Remote API](docs/remote-api.md), [exe research](docs/exe-research.md), [multiworld state](docs/multiworld-state.md)

## Prior art

**This is a work built on research done by others first.** Yoku's Island Express was already randomized before this
repository existed, and that work is what made this one possible.

- [YokuAPWorld](https://git.makuluni.com/Archipelago/YokuAPWorld) and
  [YokuArchipelagoMod](https://git.makuluni.com/Archipelago/YokuArchipelagoMod) — the Archipelago world and game mod.
  Marking a patched save, treating an id the game does not know as a placeholder pickup with its own texture, and
  much of the first map of the exe come from the mod.
- [yoku-randomizer-tracker](https://github.com/marcmagus/yoku-randomizer-tracker) — the PopTracker pack.

This is not a fork of any of them, but it would have been a great deal harder without them. Where their findings are
used, the code or docs name them.

## Credits and licensing

This project is GPL-3.0. See [LICENSE](LICENSE). Parts of the DLL are based on YokuArchipelagoMod (MIT), see
[THIRD_PARTY.md](THIRD_PARTY.md).

`src/open_yoku_rando/files/items/nothing_x102.sim` is the Randovania logo (`rdv_logo_blue.ico` from
[Randovania](https://github.com/randovania/randovania), GPL-3.0) re-encoded into the game's texture format by
`tools/make_item_texture.py`.

No game asset, game binary or decompiled game code is included in this repository.
