# Development

## Patcher

`pip install open-yoku-rando`

The patch data must match the [JSON schema](../src/open_yoku_rando/files/schema.json). There are samples in
[`tests/test_files/patcher_files`](../tests/test_files/patcher_files).

```
python -m open_yoku_rando --input-path "C:\Games\YokusIslandExpress" --input-json patch.json
```

```python
import open_yoku_rando

open_yoku_rando.patch(game_path, configuration)
```

## Tests and checks

```
uv sync --extra dev
uv run pytest
uv run mypy
uv run ruff check
```

Tests that need the game are skipped unless `YOKU_GAME_PATH` points to an install.

## The DLL

Needs Visual Studio 2022 or newer with the "Desktop development with C++" workload. It must be built with MSVC for x64
and the dynamic release CRT, because it reads and grows `std::string`/`std::vector` objects owned by the game.
Dear ImGui is a git submodule: clone with `--recurse-submodules`, or run `git submodule update --init` once.
`native\build.cmd` builds `native\build\open_yoku_rando.dll` and `xinput9_1_0.dll` and copies both into
`src/open_yoku_rando/files/native/`, where the patcher installs them from. The DLL writes `open_yoku_rando.log` next
to itself, in `open-yoku-rando/`.
