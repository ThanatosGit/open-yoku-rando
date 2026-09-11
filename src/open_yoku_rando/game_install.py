"""Reads and sanity-checks the game install."""

import csv
from pathlib import Path

from open_yoku_rando.locations import load_locations
from open_yoku_rando.logger import LOG

PATCHER_DIFFICULTY = "hard"
"""Fixed to this value because "normal" would spawn one more pickup at the beach."""

LOCATION_COLUMNS = ("Name", "Item", "Spawn ID", "Tracker", "Spawn")
_MAX_LOGGED_PROBLEMS = 20


def randomizer_csv_path(game_path: Path, difficulty: str = PATCHER_DIFFICULTY) -> Path:
    return game_path.joinpath("data", "text", f"randomizer_{difficulty}.csv")


def scripts_csv_path(game_path: Path) -> Path:
    """The randomizer's script overrides, which the game applies in every randomizer game."""
    return game_path.joinpath("data", "text", "randomizer_scripts.csv")


def dialog_anims_path(game_path: Path) -> Path:
    """The animation each dialog line is spoken with."""
    return game_path.joinpath("processed", "text", "dialog_anim.strings")


def read_randomizer_csv(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f, delimiter="\t")
        rows = list(reader)
        header = list(reader.fieldnames or [])

    missing = [column for column in LOCATION_COLUMNS if column not in header]
    if missing:
        raise ValueError(f"{path} is missing the columns {missing}")
    return header, rows


def compare_locations(rows: list[dict[str, str]]) -> list[str]:
    """The differences between the bundled locations and the rows of the PATCHER_DIFFICULTY CSV."""
    locations = load_locations()
    problems = []
    if len(rows) != len(locations):
        problems.append(f"expected {len(locations)} locations, the game has {len(rows)}")

    for i, (location, row) in enumerate(zip(locations, rows)):
        prefix = f"line {i + 2}"
        if row["Spawn ID"] != location.spawn_id:
            problems.append(f"{prefix}: spawn id is {row['Spawn ID']!r}, expected {location.spawn_id!r}")
            continue
        if row["Item"] != location.vanilla_item:
            problems.append(f"{prefix}: item is {row['Item']!r}, expected {location.vanilla_item!r}")
        if (row["Tracker"] or None) != location.tracker:
            problems.append(f"{prefix}: tracker is {row['Tracker']!r}, expected {location.tracker!r}")

    return problems


def check(game_path: Path) -> list[str]:
    """Mismatches are warnings, not errors: they most likely mean a different game version."""
    if not game_path.joinpath("Yoku.exe").is_file():
        raise ValueError(f"{game_path} is not a Yoku's Island Express install: Yoku.exe not found")

    csv_path = randomizer_csv_path(game_path)
    if not csv_path.is_file():
        raise ValueError(f"{game_path} is not a Yoku's Island Express install: {csv_path} not found")

    _, rows = read_randomizer_csv(csv_path)
    problems = compare_locations(rows)
    for problem in problems[:_MAX_LOGGED_PROBLEMS]:
        LOG.warning("%s does not match the bundled locations: %s", csv_path.name, problem)
    if len(problems) > _MAX_LOGGED_PROBLEMS:
        LOG.warning("%s: %d more mismatches", csv_path.name, len(problems) - _MAX_LOGGED_PROBLEMS)

    return problems
