"""
The seed file open_yoku_rando.dll applies when the player picks "Start Randovania Seed".

Read by `native/dll/new_game.cpp`; the two must stay in step.
"""

from pathlib import Path

from open_yoku_rando.file_util import atomic_write_bytes
from open_yoku_rando.game_files import MOD_FOLDER
from open_yoku_rando.items import game_item_id
from open_yoku_rando.locations import load_locations

SEED_FORMAT = 1
SEED_FILE_NAME = "seed.txt"


def seed_path(game_path: Path) -> Path:
    return game_path.joinpath(MOD_FOLDER, SEED_FILE_NAME)


def build(configuration: dict) -> str:
    """
    One tab-separated record per line: format, identifier, uuid, hash, seed, fruit, then `start` and `place` lines.
    """
    location_ids = {location.spawn_id: location.id for location in load_locations()}
    lines = [
        "# open-yoku-rando seed, read by open_yoku_rando.dll when a new game starts",
        f"format\t{SEED_FORMAT}",
        f"identifier\t{configuration['configuration_identifier']}",
    ]
    if "layout_uuid" in configuration:
        lines.append(f"uuid\t{configuration['layout_uuid']}")
    if "seed_hash" in configuration:
        lines.append(f"hash\t{configuration['seed_hash']}")
    lines += [
        f"seed\t{configuration['save']['seed']}",
        f"fruit\t{configuration['starting_fruit']}",
    ]
    for item, quantity in configuration["starting_items"].items():
        lines.append(f"start\t{item}\t{quantity}")
    for pickup in configuration["pickups"]:
        item = game_item_id(pickup["item"], pickup.get("model"))
        lines.append(f"place\t{location_ids[pickup['location']]}\t{item}")
    return "\n".join(lines) + "\n"


def write(game_path: Path, text: str) -> Path:
    path = seed_path(game_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    atomic_write_bytes(path, text.encode("utf-8"))
    return path
