from open_yoku_rando import items, seed_file, yoku_patcher
from open_yoku_rando.locations import load_locations


def records(text: str) -> list[list[str]]:
    assert text.endswith("\n")
    return [line.split("\t") for line in text.splitlines() if line and not line.startswith("#")]


def test_seed_file_holds_what_the_dll_reads(shuffled_configuration):
    yoku_patcher.validate(shuffled_configuration)
    shuffled_configuration["seed_hash"] = "Word Word Word (ABCD2345)"
    shuffled_configuration["starting_items"] = {"abilities/dive": 1, "wallet": 2}
    shuffled_configuration["starting_fruit"] = 7

    lines = records(seed_file.build(shuffled_configuration))

    assert lines[:6] == [
        ["format", str(seed_file.SEED_FORMAT)],
        ["identifier", shuffled_configuration["configuration_identifier"]],
        ["uuid", shuffled_configuration["layout_uuid"]],
        ["hash", "Word Word Word (ABCD2345)"],
        ["seed", str(shuffled_configuration["save"]["seed"])],
        ["fruit", "7"],
    ]
    starts = [line for line in lines if line[0] == "start"]
    assert starts == [["start", "abilities/dive", "1"], ["start", "wallet", "2"]]


def test_seed_file_places_every_location_by_id(shuffled_configuration):
    yoku_patcher.validate(shuffled_configuration)
    location_ids = {location.spawn_id: location.id for location in load_locations()}

    places = [line for line in records(seed_file.build(shuffled_configuration)) if line[0] == "place"]

    assert len(places) == len(load_locations())
    expected = {
        str(location_ids[pickup["location"]]): items.game_item_id(pickup["item"], pickup.get("model"))
        for pickup in shuffled_configuration["pickups"]
    }
    assert {location: item for _, location, item in places} == expected


def test_seed_file_leaves_out_a_missing_hash(shuffled_configuration):
    yoku_patcher.validate(shuffled_configuration)
    shuffled_configuration.pop("seed_hash", None)

    assert not [line for line in records(seed_file.build(shuffled_configuration)) if line[0] == "hash"]


def test_seed_file_lives_in_the_mod_folder(tmp_path):
    assert seed_file.seed_path(tmp_path) == tmp_path.joinpath("open-yoku-rando", "seed.txt")
