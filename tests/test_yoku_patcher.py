import logging

import jsonschema
import pytest

from open_yoku_rando import game_files, items, seed_file, yoku_patcher
from open_yoku_rando.game_install import randomizer_csv_path
from open_yoku_rando.locations import load_locations


def placed_items(game_path) -> list[str]:
    """The items of the seed file's `place` lines, in location order."""
    text = seed_file.seed_path(game_path).read_text(encoding="utf-8")
    return [line.split("\t")[2] for line in text.splitlines() if line.startswith("place\t")]


def test_validate_fills_defaults(shuffled_configuration):
    del shuffled_configuration["layout_uuid"]
    shuffled_configuration.pop("starting_items", None)
    shuffled_configuration.pop("starting_fruit", None)

    yoku_patcher.validate(shuffled_configuration)

    assert shuffled_configuration["layout_uuid"] == "00000000-0000-1111-0000-000000000000"
    assert shuffled_configuration["starting_items"] == {}
    assert shuffled_configuration["starting_fruit"] == 0


def test_validate_rejects_difficulty(shuffled_configuration):
    shuffled_configuration["save"]["difficulty"] = "veryhard"

    with pytest.raises(jsonschema.ValidationError):
        yoku_patcher.validate(shuffled_configuration)


def test_validate_rejects_duplicate_location(shuffled_configuration):
    pickups = shuffled_configuration["pickups"]
    missing = pickups[1]["location"]
    pickups[1]["location"] = pickups[0]["location"]

    with pytest.raises(ValueError, match="duplicate locations") as error:
        yoku_patcher.validate(shuffled_configuration)
    assert missing in str(error.value)


def test_validate_rejects_unknown_location(shuffled_configuration):
    shuffled_configuration["pickups"][0]["location"] = "intro_landing:1"

    with pytest.raises(ValueError, match=r"unknown locations \['intro_landing:1'\]"):
        yoku_patcher.validate(shuffled_configuration)


def test_validate_rejects_missing_pickups(shuffled_configuration):
    shuffled_configuration["pickups"].pop()

    with pytest.raises(jsonschema.ValidationError):
        yoku_patcher.validate(shuffled_configuration)


def test_validate_rejects_unknown_item(shuffled_configuration):
    shuffled_configuration["pickups"][0]["item"] = "abilities/flying"

    with pytest.raises(jsonschema.ValidationError):
        yoku_patcher.validate(shuffled_configuration)


@pytest.mark.parametrize("item", ["reward_fruit_big", "reward_fruit_medium"])
def test_validate_rejects_ignored_starting_items(shuffled_configuration, item):
    # the game keeps fruit in _global.items but ignores it
    shuffled_configuration["starting_items"] = {item: 1}

    with pytest.raises(jsonschema.ValidationError):
        yoku_patcher.validate(shuffled_configuration)


@pytest.mark.parametrize("item", ["tadpole", "nugget", "traitor_spirit", "dustbunny_dirty", "wallet", "wickerling"])
def test_validate_accepts_counted_starting_items(shuffled_configuration, item):
    shuffled_configuration["starting_items"] = {item: 3}

    yoku_patcher.validate(shuffled_configuration)


def test_validate_rejects_starting_item_quantity(shuffled_configuration):
    shuffled_configuration["starting_items"] = {"wallet": 0}

    with pytest.raises(jsonschema.ValidationError):
        yoku_patcher.validate(shuffled_configuration)


def test_validate_rejects_negative_starting_fruit(shuffled_configuration):
    shuffled_configuration["starting_fruit"] = -1

    with pytest.raises(jsonschema.ValidationError):
        yoku_patcher.validate(shuffled_configuration)


def test_patch_game_writes_the_seed(fake_game_path, shuffled_configuration):
    yoku_patcher.patch_game(fake_game_path, shuffled_configuration)

    seed = seed_file.seed_path(fake_game_path)
    assert seed.read_text(encoding="utf-8") == seed_file.build(shuffled_configuration)
    assert fake_game_path.joinpath("xinput9_1_0.dll").is_file()


def test_patch_game_warns_on_game_mismatch(fake_game_path, vanilla_configuration, caplog):
    csv_path = randomizer_csv_path(fake_game_path)
    csv_path.write_bytes(csv_path.read_bytes().replace(b"\treward_fruit_big\t", b"\twickerling\t", 1))

    with caplog.at_level(logging.WARNING, logger="yoku_patcher"):
        yoku_patcher.patch_game(fake_game_path, vanilla_configuration)

    assert "item is 'wickerling', expected 'reward_fruit_big'" in caplog.text
    assert seed_file.seed_path(fake_game_path).is_file()


def test_patch_game_rejects_non_install(tmp_path, vanilla_configuration):
    with pytest.raises(ValueError, match="Yoku.exe not found"):
        yoku_patcher.patch_game(tmp_path, vanilla_configuration)
    assert not tmp_path.joinpath(game_files.MOD_FOLDER).exists()


def test_validate_accepts_a_nothing_pickup(shuffled_configuration):
    shuffled_configuration["pickups"][0]["item"] = game_files.NOTHING_ITEM_ID

    yoku_patcher.validate(shuffled_configuration)


def test_validate_rejects_a_nothing_starting_item(shuffled_configuration):
    # Another player's item is never something this player starts with.
    shuffled_configuration["starting_items"] = {game_files.NOTHING_ITEM_ID: 1}

    with pytest.raises(jsonschema.ValidationError):
        yoku_patcher.validate(shuffled_configuration)


def test_patch_changes_no_file_of_the_game(fake_game_path, vanilla_configuration):
    game_strings = fake_game_path.joinpath("processed", "text", "dialog_en.strings")
    before = game_strings.read_bytes()
    vanilla_configuration["pickups"][0]["item"] = game_files.NOTHING_ITEM_ID

    yoku_patcher.patch_game(fake_game_path, vanilla_configuration)

    assert game_strings.read_bytes() == before
    assert not fake_game_path.joinpath("processed", "items").exists()


def test_a_caption_on_a_real_item_is_written_and_one_without_is_not(
    fake_game_path, vanilla_configuration
):
    vanilla_configuration["pickups"][0]["caption"] = "A real item with a line of its own"
    vanilla_configuration["pickups"][1].pop("caption", None)

    yoku_patcher.patch_game(fake_game_path, vanilla_configuration)

    texts = fake_game_path.joinpath(game_files.TEXTS_PATH).read_text(encoding="utf-8")
    first, second = load_locations()[0].id, load_locations()[1].id
    assert f"items/nothing_{first}_1;A real item with a line of its own" in texts
    assert f"items/nothing_{second}_1" not in texts


def test_validate_rejects_a_model_on_a_real_item(shuffled_configuration):
    shuffled_configuration["pickups"][0]["item"] = "toolbox"
    shuffled_configuration["pickups"][0]["model"] = items.DEFAULT_MODEL

    with pytest.raises(ValueError, match="only applies to"):
        yoku_patcher.validate(shuffled_configuration)


def test_validate_rejects_a_model_the_patcher_does_not_ship(shuffled_configuration):
    shuffled_configuration["pickups"][0]["item"] = items.NOTHING_ITEM_ID
    shuffled_configuration["pickups"][0]["model"] = "no_such_model"

    with pytest.raises(ValueError, match="unknown models"):
        yoku_patcher.validate(shuffled_configuration)


def test_the_default_model_is_shipped():
    assert items.DEFAULT_MODEL in game_files.available_models()


def test_a_model_decides_the_id_written_into_the_save():
    assert items.game_item_id(items.NOTHING_ITEM_ID, None) == items.NOTHING_ITEM_ID
    assert items.game_item_id(items.NOTHING_ITEM_ID, items.DEFAULT_MODEL) == items.NOTHING_ITEM_ID
    assert items.game_item_id(items.NOTHING_ITEM_ID, "other_game") == "nothing_other_game"
    assert items.game_item_id("toolbox", None) == "toolbox"


def test_a_nothing_with_a_model_installs_that_model_under_its_own_id(
    fake_game_path, vanilla_configuration
):
    # Only the shipped model exists, so point a second pickup at it by name and check it lands under its own id.
    vanilla_configuration["pickups"][0]["item"] = items.NOTHING_ITEM_ID
    vanilla_configuration["pickups"][1]["item"] = items.NOTHING_ITEM_ID
    vanilla_configuration["pickups"][1]["model"] = items.DEFAULT_MODEL

    yoku_patcher.patch_game(fake_game_path, vanilla_configuration)

    assert fake_game_path.joinpath(game_files.ITEMS_FOLDER, f"{items.NOTHING_ITEM_ID}_x102.sim").is_file()
    assert placed_items(fake_game_path)[:2] == [items.NOTHING_ITEM_ID, items.NOTHING_ITEM_ID]


def test_a_named_model_gets_its_own_id_and_text(fake_game_path, vanilla_configuration, monkeypatch):
    # Pretend the package ships a second model, so the id, the installed file and the text keys can be checked.
    monkeypatch.setattr(game_files, "available_models", lambda: ["other_game", items.DEFAULT_MODEL])
    monkeypatch.setattr(
        game_files,
        "installed_file_source",
        lambda relative: game_files.files_path().joinpath(f"items/{items.DEFAULT_MODEL}_x102.sim"),
    )
    vanilla_configuration["pickups"][0]["item"] = items.NOTHING_ITEM_ID
    vanilla_configuration["pickups"][0]["model"] = "other_game"
    vanilla_configuration["pickups"][0]["caption"] = "You found another player's item!"

    yoku_patcher.patch_game(fake_game_path, vanilla_configuration)

    assert fake_game_path.joinpath(game_files.ITEMS_FOLDER, "nothing_other_game_x102.sim").is_file()
    assert placed_items(fake_game_path)[0] == "nothing_other_game"

    # The captions and the default line hang off the base id, because the DLL collapses a model id back to it at
    # pickup. Name and description are repeated under the model id for a pickup that skips that hook.
    location_id = load_locations()[0].id
    strings = fake_game_path.joinpath(game_files.TEXTS_PATH).read_text(encoding="utf-8")
    assert f"items/nothing_{location_id}_1;You found another player's item!" in strings
    assert "items/nothing_other_game_name;Nothing" in strings
    assert "items/nothing_other_game_1;" not in strings
    assert f"items/nothing_other_game_{location_id}_1" not in strings


def test_patch_with_a_nothing_pickup_installs_the_texture(
    fake_game_path, vanilla_configuration
):
    vanilla_configuration["pickups"][0]["item"] = game_files.NOTHING_ITEM_ID

    yoku_patcher.patch_game(fake_game_path, vanilla_configuration)

    target = fake_game_path.joinpath(game_files.ITEMS_FOLDER, f"{game_files.NOTHING_ITEM_ID}_x102.sim")
    assert target.is_file()
