import json

import pytest

from open_yoku_rando import game_files, game_install
from open_yoku_rando.files import files_path
from open_yoku_rando.locations import load_locations, make_id

DIFFICULTIES = ("normal", "hard", "veryhard")
"""Every CSV the game ships. The generator and the patcher only ever read PATCHER_DIFFICULTY, so these
tests are what keeps the other two from drifting away from it unnoticed."""


def _item_enum() -> set[str]:
    schema = json.loads(files_path().joinpath("schema.json").read_text(encoding="utf-8"))
    return set(schema["$defs"]["item_id"]["enum"])


def test_locations_are_consistent():
    locations = load_locations()

    assert len(locations) == 248
    assert len({location.spawn_id for location in locations}) == 248
    assert len({location.id for location in locations}) == 248

    level_numbers: dict[str, int] = {}
    for location in locations:
        assert location.spawn_id == f"{location.level}:{location.object_id}"
        assert location.id == make_id(location.level_number, location.object_id)
        assert 0 < location.object_id <= 0xFFFF
        assert level_numbers.setdefault(location.level, location.level_number) == location.level_number
    assert len(set(level_numbers.values())) == len(level_numbers)


def test_vanilla_items_are_in_schema():
    items = {location.vanilla_item for location in load_locations()}

    assert items <= _item_enum()


def test_locations_match_game(yoku_game_path):
    _, rows = game_install.read_randomizer_csv(game_install.randomizer_csv_path(yoku_game_path))

    assert game_install.compare_locations(rows) == []
    assert game_install.check(yoku_game_path) == []
    assert [row["Name"] for row in rows] == [location.name for location in load_locations()]
    assert [row["Spawn"] == "1" for row in rows] == [location.spawn for location in load_locations()]


@pytest.mark.parametrize("difficulty", DIFFICULTIES)
def test_difficulties_share_locations(yoku_game_path, difficulty):
    _, rows = game_install.read_randomizer_csv(game_install.randomizer_csv_path(yoku_game_path, difficulty))

    assert [row["Spawn ID"] for row in rows] == [location.spawn_id for location in load_locations()]


def test_item_enum_matches_game(yoku_game_path):
    items: set[str] = set()
    for difficulty in DIFFICULTIES:
        header, rows = game_install.read_randomizer_csv(game_install.randomizer_csv_path(yoku_game_path, difficulty))
        items.update(row["Item"] for row in rows)
        items.update(column.split(":")[0] for column in header if column not in game_install.LOCATION_COLUMNS)

    # NOTHING_ITEM_ID is ours, so it is in no CSV.
    assert _item_enum() == items | {game_files.NOTHING_ITEM_ID}
