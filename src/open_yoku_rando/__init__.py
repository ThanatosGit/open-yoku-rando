from pathlib import Path

from open_yoku_rando.patch_util import patch_with_status_update


def patch(input_path: Path, configuration: dict) -> None:
    from open_yoku_rando.yoku_patcher import patch_game
    return patch_game(input_path, configuration)


__all__ = [
    "patch",
    "patch_with_status_update",
]
