import pytest

import open_yoku_rando
from open_yoku_rando import cli, running_game
from open_yoku_rando.version import version


def test_version():
    assert isinstance(version, str)


def test_main(fake_game_path, test_files_dir, mocker):
    setup_logging = mocker.patch("open_yoku_rando.cli.setup_logging")

    cli.main([
        "--input-path", str(fake_game_path),
        "--input-json", str(test_files_dir.joinpath("patcher_files", "shuffled.json")),
    ])

    setup_logging.assert_called_once()
    assert fake_game_path.joinpath("open-yoku-rando", "seed.txt").is_file()
    assert fake_game_path.joinpath("xinput9_1_0.dll").is_file()


def test_patch(fake_game_path, vanilla_configuration):
    open_yoku_rando.patch(fake_game_path, vanilla_configuration)

    assert fake_game_path.joinpath("open-yoku-rando", "seed.txt").is_file()


def test_patch_with_status_update(fake_game_path, vanilla_configuration):
    updates: list[tuple[float, str]] = []

    open_yoku_rando.patch_with_status_update(
        fake_game_path, vanilla_configuration, lambda progress, message: updates.append((progress, message)),
    )

    assert updates[-1][0] == 1
    assert [progress for progress, _ in updates] == sorted(progress for progress, _ in updates)
    assert any("Wrote the seed file" in message for _, message in updates)
    assert not open_yoku_rando.logger.LOG.handlers


def test_main_refuses_a_running_game(fake_game_path, test_files_dir, mocker, game_not_running):
    mocker.patch("open_yoku_rando.cli.setup_logging")
    game_not_running.return_value = running_game.GameProcessState.RUNNING

    with pytest.raises(running_game.GameIsRunningError):
        cli.main([
            "--input-path", str(fake_game_path),
            "--input-json", str(test_files_dir.joinpath("patcher_files", "vanilla.json")),
        ])

    assert not fake_game_path.joinpath("open-yoku-rando").exists()
