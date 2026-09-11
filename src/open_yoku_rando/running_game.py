import ctypes
import enum
import subprocess
import sys
from pathlib import Path, PureWindowsPath

from open_yoku_rando.logger import LOG

EXECUTABLE_NAME = "Yoku.exe"
"""There is no native Linux build; under Proton/Wine the process still carries this name."""


class GameProcessState(enum.Enum):
    RUNNING = "running"
    NOT_RUNNING = "not_running"
    UNKNOWN = "unknown"
    """The platform has no supported way to look, or looking failed."""


class GameIsRunningError(Exception):
    pass


def game_process_state() -> GameProcessState:
    """Looks for a running `Yoku.exe`. Never raises."""
    try:
        if sys.platform == "win32":
            return _windows_state()
        if sys.platform.startswith("linux"):
            return _linux_state()
    except Exception as error:  # a failed check must never fail the patch
        LOG.warning("Could not check whether %s is running: %s", EXECUTABLE_NAME, error)
        return GameProcessState.UNKNOWN

    LOG.warning("Cannot check whether %s is running on this platform (%s)", EXECUTABLE_NAME, sys.platform)
    return GameProcessState.UNKNOWN


def ensure_not_running() -> GameProcessState:
    """Raises GameIsRunningError if the game is running. No override: the game would overwrite the patch."""
    state = game_process_state()

    if state is GameProcessState.RUNNING:
        raise GameIsRunningError(
            f"{EXECUTABLE_NAME} is running. Close Yoku's Island Express and try again: the game keeps the save in "
            f"memory and writes it back on its own, so it would undo the patch."
        )
    if state is GameProcessState.UNKNOWN:
        LOG.warning("Could not tell whether %s is running; make sure the game is closed", EXECUTABLE_NAME)

    return state


_TH32CS_SNAPPROCESS = 0x00000002
_INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
_MAX_PATH = 260


class _ProcessEntry32W(ctypes.Structure):
    # PROCESSENTRY32W of tlhelp32.h; c_void_p stands in for its pointer-sized ULONG_PTR fields.
    _fields_ = (
        ("dwSize", ctypes.c_uint32),
        ("cntUsage", ctypes.c_uint32),
        ("th32ProcessID", ctypes.c_uint32),
        ("th32DefaultHeapID", ctypes.c_void_p),
        ("th32ModuleID", ctypes.c_uint32),
        ("cntThreads", ctypes.c_uint32),
        ("th32ParentProcessID", ctypes.c_uint32),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", ctypes.c_uint32),
        ("szExeFile", ctypes.c_wchar * _MAX_PATH),
    )


def _windows_state() -> GameProcessState:
    try:
        return _windows_state_via_toolhelp()
    except OSError as error:
        LOG.debug("The process snapshot failed (%s), falling back to tasklist", error)
        return _windows_state_via_tasklist()


def _windows_state_via_toolhelp() -> GameProcessState:
    # ctypes.windll only exists on Windows, and typeshed knows it, so it is fetched dynamically.
    kernel32 = getattr(ctypes, "WinDLL")("kernel32", use_last_error=True)
    last_error = getattr(ctypes, "get_last_error")
    snapshot = kernel32.CreateToolhelp32Snapshot(_TH32CS_SNAPPROCESS, 0)
    if snapshot == _INVALID_HANDLE_VALUE:
        raise OSError(last_error(), "CreateToolhelp32Snapshot failed")

    try:
        entry = _ProcessEntry32W()
        entry.dwSize = ctypes.sizeof(_ProcessEntry32W)
        if not kernel32.Process32FirstW(snapshot, ctypes.byref(entry)):
            raise OSError(last_error(), "Process32FirstW failed")

        while True:
            if entry.szExeFile.casefold() == EXECUTABLE_NAME.casefold():
                return GameProcessState.RUNNING
            if not kernel32.Process32NextW(snapshot, ctypes.byref(entry)):
                return GameProcessState.NOT_RUNNING
    finally:
        kernel32.CloseHandle(snapshot)


def _windows_state_via_tasklist() -> GameProcessState:
    result = subprocess.run(
        ["tasklist", "/NH", "/FI", f"IMAGENAME eq {EXECUTABLE_NAME}"],
        capture_output=True, text=True, check=True, timeout=30,
    )
    # With no match tasklist prints an "INFO: No tasks ..." line instead of a table, so look for the name itself.
    if EXECUTABLE_NAME.casefold() in result.stdout.casefold():
        return GameProcessState.RUNNING
    return GameProcessState.NOT_RUNNING


def _linux_state(proc_path: Path | None = None) -> GameProcessState:
    proc = proc_path or Path("/proc")
    for entry in proc.iterdir():
        if not entry.name.isdigit():
            continue
        if _is_game_process(entry):
            return GameProcessState.RUNNING
    return GameProcessState.NOT_RUNNING


def _is_game_process(process_dir: Path) -> bool:
    # comm truncates to 15 characters, which "Yoku.exe" survives; Wine also keeps the exe in cmdline.
    name = _read_proc_file(process_dir.joinpath("comm")).strip()
    if name.casefold() == EXECUTABLE_NAME.casefold():
        return True

    cmdline = _read_proc_file(process_dir.joinpath("cmdline"))
    return any(
        PureWindowsPath(argument).name.casefold() == EXECUTABLE_NAME.casefold()
        for argument in cmdline.split("\0") if argument
    )


def _read_proc_file(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:  # the process ended, or it belongs to another user
        return ""
