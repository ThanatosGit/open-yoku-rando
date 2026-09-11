import zlib

import pytest

from open_yoku_rando import game_files

TARGET = f"{game_files.ITEMS_FOLDER}/{game_files.NOTHING_ITEM_ID}_x102.sim"
TEXTS = game_files.TEXTS_PATH
DLL = f"{game_files.MOD_FOLDER}/open_yoku_rando.dll"
PROXY = "xinput9_1_0.dll"
GAME_STRINGS = "processed/text/dialog_en.strings"


def default_models() -> dict[str, str]:
    return {game_files.NOTHING_ITEM_ID: game_files.DEFAULT_MODEL}


def shipped_bytes() -> bytes:
    return game_files.installed_file_source(game_files.installed_files(default_models())[TARGET]).read_bytes()


def game_files_snapshot(game_path) -> dict[str, bytes]:
    """Every file outside the mod's own, as it is now."""
    return {
        path.relative_to(game_path).as_posix(): path.read_bytes()
        for path in game_path.rglob("*")
        if path.is_file()
        and path.relative_to(game_path).parts[0] != game_files.MOD_FOLDER
        and path.name != PROXY
    }


def test_the_shipped_texture_is_a_sim_the_game_can_read():
    data = shipped_bytes()
    magic, width, height, padded_width, padded_height, fmt, zero, uncompressed, compressed = (
        int.from_bytes(data[i:i + 4], "little") for i in range(0, 0x24, 4)
    )
    assert magic == 0x73696D0B
    assert (fmt, zero) == (7, 0)
    assert (padded_width, padded_height) == ((width + 3) // 4 * 4, (height + 3) // 4 * 4)
    assert compressed == len(data) - 0x24

    # The game reads the mip chain from one zlib stream and trusts these two sizes.
    assert len(zlib.decompress(data[0x24:])) == uncompressed


def test_install_puts_everything_into_the_mod_folder(fake_game_path):
    install = game_files.install_files(fake_game_path, default_models(), {})

    assert sorted(install.written) == sorted([DLL, PROXY, TARGET, TEXTS])
    assert install.removed == ()
    assert fake_game_path.joinpath(TARGET).read_bytes() == shipped_bytes()
    assert fake_game_path.joinpath(DLL).is_file()
    assert fake_game_path.joinpath(PROXY).is_file()


def test_install_changes_no_file_of_the_game(fake_game_path):
    before = game_files_snapshot(fake_game_path)

    game_files.install_files(fake_game_path, default_models(), {42: "A caption"})

    assert game_files_snapshot(fake_game_path) == before
    assert not fake_game_path.joinpath("processed", "items").exists()


def test_install_writes_the_texts(fake_game_path):
    game_files.install_files(fake_game_path, default_models(), {42: "Line one\nLine two; and more"})

    text = fake_game_path.joinpath(TEXTS).read_bytes().decode("utf-8")
    assert text.endswith("\r\n")
    lines = text.split("\r\n")[:-1]
    assert "items/nothing_name;Nothing" in lines
    # The game's .strings format: no real line break and no semicolon inside a value.
    assert "items/nothing_42_1;Line one\\nLine two, and more" in lines


def test_install_removes_models_it_no_longer_uses(fake_game_path):
    old = fake_game_path.joinpath(game_files.ITEMS_FOLDER, "nothing_missiles_x102.sim")
    old.parent.mkdir(parents=True)
    old.write_bytes(b"an earlier seed's model")

    install = game_files.install_files(fake_game_path, default_models(), {})

    assert not old.exists()
    assert install.removed == (f"{game_files.ITEMS_FOLDER}/nothing_missiles_x102.sim",)
    assert fake_game_path.joinpath(TARGET).is_file()


def test_installing_twice_gives_the_same_texts(fake_game_path):
    game_files.install_files(fake_game_path, default_models(), {42: "A caption"})
    once = fake_game_path.joinpath(TEXTS).read_bytes()

    game_files.install_files(fake_game_path, default_models(), {42: "A caption"})

    assert fake_game_path.joinpath(TEXTS).read_bytes() == once


def test_install_explains_missing_dlls(fake_game_path, mocker, tmp_path):
    mocker.patch("open_yoku_rando.game_files.files_path", return_value=tmp_path)

    with pytest.raises(ValueError, match=r"native/build\.cmd"):
        game_files.install_files(fake_game_path, {}, {})


def test_nothing_strings_default_model_only():
    keys = [key for key, _ in game_files.nothing_strings({}, ("nothing",))]

    assert keys == ["items/nothing_name", "items/nothing_desc", "items/nothing_1"]


def test_nothing_strings_names_every_model():
    # A pickup that skips the DLL's pickup_dialog hook keeps `nothing_missiles` in the inventory.
    entries = dict(game_files.nothing_strings({}, ("nothing", "nothing_missiles")))

    assert entries["items/nothing_missiles_name"] == entries["items/nothing_name"]
    assert entries["items/nothing_missiles_desc"] == entries["items/nothing_desc"]
    assert "items/nothing_missiles_1" not in entries


def test_nothing_strings_keeps_the_captions_on_the_base_id():
    # `nothing_missiles` is collapsed back to `nothing` before a caption is swapped in.
    entries = game_files.nothing_strings({42: "Another player's item"}, ("nothing", "nothing_missiles"))
    keys = [key for key, _ in entries]

    assert "items/nothing_42_1" in keys
    assert "items/nothing_missiles_42_1" not in keys


CEREMONY = b'function self.instruments_2() triggerOutput("visit_nim") dialogBegin("nim") dialogLine(94) dialogEnd() end'
GAME_ANIMS = b"nim_94;talk_1,hold_convo_idle\r\nnim_95;talk_1,hold_convo_idle\0"


def add_beacon_sources(game_path) -> None:
    """The two game files the beacon goal starts from."""
    game_path.joinpath("data", "text", "randomizer_scripts.csv").write_bytes(b"scripts\t" + CEREMONY + b"\r\n")
    game_path.joinpath("processed", "text", "dialog_anim.strings").write_bytes(GAME_ANIMS)


def test_gate_ceremony_refuses_until_the_required_beacon_is_lit():
    gated = game_files.gate_ceremony(CEREMONY, 5)

    assert gated == CEREMONY.replace(
        b'dialogBegin("nim")',
        b'dialogBegin("nim") if hasCompletedQuest("@open-yoku-rando:beacon:5") == 0 then'
        b" dialogLine(900) dialogEnd() return end",
    )


@pytest.mark.parametrize("scripts", [b"no ceremony here", CEREMONY + CEREMONY])
def test_gate_ceremony_needs_exactly_one_ceremony(scripts):
    with pytest.raises(ValueError, match="Nim's ceremony"):
        game_files.gate_ceremony(scripts, 1)


@pytest.mark.parametrize(("required", "expected"), [(1, "1 beacon"), (5, "5 beacons")])
def test_beacon_hint_counts_the_beacons(required, expected):
    assert f"<style1>{expected}</style>" in game_files.beacon_hint(required)


@pytest.mark.parametrize(
    ("anims", "expected"),
    [
        # The game's own file: a NUL and no final line break.
        (
            GAME_ANIMS,
            (
                b"nim_94;talk_1,hold_convo_idle\r\nnim_95;talk_1,hold_convo_idle\r\n"
                b"nim_900;talk_1,hold_convo_idle\r\n\0"
            ),
        ),
        (b"nim_94;talk_1,hold_convo_idle\r\n", b"nim_94;talk_1,hold_convo_idle\r\nnim_900;talk_1,hold_convo_idle\r\n"),
    ],
)
def test_animate_hint_adds_nims_line_before_the_end(anims, expected):
    assert game_files.animate_hint(anims) == expected


def test_install_with_beacons_serves_the_gated_ceremony(fake_game_path):
    add_beacon_sources(fake_game_path)
    before = game_files_snapshot(fake_game_path)

    install = game_files.install_files(fake_game_path, default_models(), {}, required_beacons=3)

    assert {game_files.SCRIPTS_PATH, game_files.ANIMS_PATH} <= set(install.written)
    scripts = fake_game_path.joinpath(game_files.SCRIPTS_PATH).read_bytes()
    assert b'hasCompletedQuest("@open-yoku-rando:beacon:3") == 0' in scripts
    anims = fake_game_path.joinpath(game_files.ANIMS_PATH).read_bytes()
    assert b"nim_900;talk_1,hold_convo_idle\r\n" in anims
    texts = fake_game_path.joinpath(TEXTS).read_bytes().decode("utf-8").split("\r\n")
    assert f"nim_900;{game_files.beacon_hint(3)}" in texts
    assert game_files_snapshot(fake_game_path) == before


def test_install_without_beacons_removes_the_beacon_files(fake_game_path):
    add_beacon_sources(fake_game_path)
    game_files.install_files(fake_game_path, default_models(), {}, required_beacons=3)

    install = game_files.install_files(fake_game_path, default_models(), {})

    assert install.removed == (game_files.SCRIPTS_PATH, game_files.ANIMS_PATH)
    assert not fake_game_path.joinpath(game_files.SCRIPTS_PATH).exists()
    assert not fake_game_path.joinpath(game_files.ANIMS_PATH).exists()
    assert "nim_900" not in fake_game_path.joinpath(TEXTS).read_text(encoding="utf-8")
