# How a seed gets into the game

1. Randovania exports to the game folder, the folder with `Yoku.exe`. The patcher adds `xinput9_1_0.dll` there and
   puts everything else into `open-yoku-rando/`: `open_yoku_rando.dll`, `seed.txt` (placement, starting items, fruit,
   seed and hash), `texts.strings` (the location texts), `items/` (models) and, when the seed requires beacons,
   `randomizer_scripts.csv` (the game's own, with Nim's ceremony waiting for them) and `dialog_anim.strings` (the
   game's own, with an animation for Nim's hint). No file of the game is changed.
2. Start the game the usual way: Steam, GOG Galaxy, Epic or `Yoku.exe` itself. All three builds import
   `xinput9_1_0.dll`, which the game does not ship, so Windows loads the one in the game folder, and that starts
   `open-yoku-rando/open_yoku_rando.dll`. The DLL adds the texts to the game's dialog table and hands the game
   `open-yoku-rando/items/<name>` whenever it opens `processed/items/<name>` and that file exists, so a model there
   also replaces the model of an existing item; `randomizer_scripts.csv` and `dialog_anim.strings` are served the
   same way.
3. Choose an empty save slot. It offers a single entry, **"Start Randovania Seed: \<seed hash\>"**, which starts the
   seed. Without a seed file the entry says "No Randovania seed found" and does nothing. A slot started this way is
   labelled with its seed hash.

The game saves wherever it always does, so Steam's cloud saves work. Patch while the game is closed: the patcher
checks for a running `Yoku.exe` and stops otherwise. Deleting `xinput9_1_0.dll` and `open-yoku-rando/` removes the
mod completely.

Further reading:
- [remote-api.md](remote-api.md) — the DLL's socket protocol and its Lua functions;
- [exe-research.md](exe-research.md) — the static analysis of `Yoku.exe` the DLL rests on;
- [multiworld-state.md](multiworld-state.md) — where each multiworld packet gets its contents.
