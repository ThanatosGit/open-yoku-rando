import json
from pathlib import Path

from open_yoku_rando import game_files, game_install, items, running_game, seed_file
from open_yoku_rando.files import files_path
from open_yoku_rando.locations import load_locations
from open_yoku_rando.logger import LOG
from open_yoku_rando.validator_with_default import DefaultValidatingDraft7Validator


def _read_schema() -> dict:
    with files_path().joinpath("schema.json").open(encoding="utf-8") as f:
        return json.load(f)


def validate(configuration: dict) -> None:
    DefaultValidatingDraft7Validator(_read_schema()).validate(configuration)

    known = {location.spawn_id for location in load_locations()}
    seen: set[str] = set()
    unknown = []
    duplicates = []
    for pickup in configuration["pickups"]:
        spawn_id = pickup["location"]
        if spawn_id not in known:
            unknown.append(spawn_id)
        elif spawn_id in seen:
            duplicates.append(spawn_id)
        seen.add(spawn_id)
    missing = [location.spawn_id for location in load_locations() if location.spawn_id not in seen]

    # `model` only means something for a Nothing: every other item brings its own model with it.
    bad_models = []
    misplaced_models = []
    known_models = game_files.available_models()
    for pickup in configuration["pickups"]:
        model = pickup.get("model")
        if model is None:
            continue
        if pickup["item"] != items.NOTHING_ITEM_ID:
            misplaced_models.append(pickup["location"])
        elif model not in known_models:
            bad_models.append(model)

    errors = []
    if unknown:
        errors.append(f"unknown locations {unknown}")
    if duplicates:
        errors.append(f"duplicate locations {duplicates}")
    if missing:
        errors.append(f"missing locations {missing}")
    if misplaced_models:
        errors.append(f"a model only applies to a \"{items.NOTHING_ITEM_ID}\" pickup, but {misplaced_models} set one")
    if bad_models:
        errors.append(f"unknown models {sorted(set(bad_models))}; this patcher ships {known_models}")
    if errors:
        raise ValueError(f"Invalid pickups: {'; '.join(errors)}")


def patch_game(input_path: Path, configuration: dict) -> None:
    """Writes the seed file and the DLLs into the game folder."""
    LOG.info("Validating configuration")
    validate(configuration)

    LOG.info("Checking that the game is not running")
    running_game.ensure_not_running()

    LOG.info("Checking game install at %s", input_path)
    game_install.check(input_path)

    location_ids = {location.spawn_id: location.id for location in load_locations()}
    # Models by game item id (the game builds the model path from it), captions by location.
    models: dict[str, str] = {}
    captions: dict[int, str] = {}
    for pickup in configuration["pickups"]:
        location_id = location_ids[pickup["location"]]
        if pickup["item"] == items.NOTHING_ITEM_ID:
            model = pickup.get("model") or items.DEFAULT_MODEL
            models[items.game_item_id(pickup["item"], model)] = model
            captions[location_id] = pickup.get("caption") or game_files.NOTHING_DEFAULT_TEXT
        elif "caption" in pickup:
            captions[location_id] = pickup["caption"]

    # Always, so a previous seed's texts and models never linger.
    LOG.info("%d Nothing item(s) in %d model(s), %d location caption(s)",
             sum(1 for pickup in configuration["pickups"] if pickup["item"] == items.NOTHING_ITEM_ID), len(models),
             len(captions))
    required_beacons = configuration["required_beacons"]
    if required_beacons:
        LOG.info("Nim starts the ceremony once %d beacon(s) are lit", required_beacons)
    install = game_files.install_files(input_path, models, captions, required_beacons)
    LOG.info("Installed %d file(s), removed %d unused file(s); no file of the game was changed",
             len(install.written), len(install.removed))

    path = seed_file.write(input_path, seed_file.build(configuration))
    LOG.info("Wrote the seed file %s; start the game and pick an empty slot", path)

    LOG.info("Done")
