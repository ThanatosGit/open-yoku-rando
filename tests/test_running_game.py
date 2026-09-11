import subprocess
import sys

import pytest

from open_yoku_rando import running_game
from open_yoku_rando.running_game import GameProcessState


def make_proc(root, processes: dict[str, tuple[str, list[str]]]):
    """Builds a fake /proc: pid -> (comm, cmdline)."""
    proc = root.joinpath("proc")
    proc.mkdir()
    for pid, (comm, cmdline) in processes.items():
        process_dir = proc.joinpath(pid)
        process_dir.mkdir()
        process_dir.joinpath("comm").write_text(comm + "\n", encoding="utf-8")
        process_dir.joinpath("cmdline").write_bytes("\0".join(cmdline).encode("utf-8"))
    # /proc also holds entries that are not processes.
    proc.joinpath("self").mkdir()
    return proc


def test_linux_state_finds_it_by_comm(tmp_path):
    proc = make_proc(tmp_path, {"1": ("systemd", ["/sbin/init"]), "42": ("Yoku.exe", [])})

    assert running_game._linux_state(proc) is GameProcessState.RUNNING


def test_linux_state_finds_it_in_a_proton_cmdline(tmp_path):
    proc = make_proc(tmp_path, {
        "42": ("wine64-preloader", [r"Z:\home\p\.steam\steamapps\common\Yoku's Island Express\Yoku.exe"]),
    })

    assert running_game._linux_state(proc) is GameProcessState.RUNNING


def test_linux_state_without_the_game(tmp_path):
    proc = make_proc(tmp_path, {"1": ("systemd", ["/sbin/init"]), "7": ("steam", ["/usr/bin/steam", "yoku.exe.log"])})

    assert running_game._linux_state(proc) is GameProcessState.NOT_RUNNING


def test_linux_state_ignores_processes_that_disappear(tmp_path):
    proc = make_proc(tmp_path, {"42": ("Yoku.exe", [])})
    proc.joinpath("42", "comm").unlink()
    proc.joinpath("42", "cmdline").unlink()

    assert running_game._linux_state(proc) is GameProcessState.NOT_RUNNING


@pytest.mark.parametrize(("stdout", "expected"), [
    ("Yoku.exe                      1234 Console                    1    123.456 K\n", GameProcessState.RUNNING),
    ("INFO: No tasks are running which match the specified criteria.\n", GameProcessState.NOT_RUNNING),
])
def test_windows_state_via_tasklist(mocker, stdout, expected):
    mocker.patch("subprocess.run", return_value=subprocess.CompletedProcess([], 0, stdout=stdout, stderr=""))

    assert running_game._windows_state_via_tasklist() is expected


def test_game_process_state_on_an_unsupported_platform(mocker):
    mocker.stopall()  # this test wants the real check
    mocker.patch.object(sys, "platform", "darwin")

    assert running_game.game_process_state() is GameProcessState.UNKNOWN


def test_game_process_state_survives_a_failing_check(mocker):
    mocker.stopall()  # this test wants the real check
    mocker.patch.object(sys, "platform", "linux")
    mocker.patch("open_yoku_rando.running_game._linux_state", side_effect=OSError("nope"))

    assert running_game.game_process_state() is GameProcessState.UNKNOWN


def test_game_process_state_on_this_machine(mocker):
    """The real check must answer without raising, whatever it finds."""
    mocker.stopall()

    assert running_game.game_process_state() in GameProcessState


def test_ensure_not_running_passes(game_not_running):
    assert running_game.ensure_not_running() is GameProcessState.NOT_RUNNING


def test_ensure_not_running_raises(game_not_running):
    game_not_running.return_value = GameProcessState.RUNNING

    with pytest.raises(running_game.GameIsRunningError, match="Yoku.exe is running"):
        running_game.ensure_not_running()


def test_ensure_not_running_allows_an_unknown_state(game_not_running):
    game_not_running.return_value = GameProcessState.UNKNOWN

    assert running_game.ensure_not_running() is GameProcessState.UNKNOWN
