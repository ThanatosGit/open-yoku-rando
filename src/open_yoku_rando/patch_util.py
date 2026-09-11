import logging
import typing
from pathlib import Path

from open_yoku_rando.logger import LOG


def patch_with_status_update(input_path: Path, configuration: dict,
                             status_update: typing.Callable[[float, str], None]) -> None:
    from open_yoku_rando.yoku_patcher import patch_game
    total_logs = 7  # the LOG.info calls in patch_game; warnings may add more, hence the clamp below

    class StatusUpdateHandler(logging.Handler):
        count = 0

        def emit(self, record: logging.LogRecord) -> None:
            message = self.format(record)

            self.count += 1
            status_update(min(self.count / total_logs, 1.0), message)

    new_handler = StatusUpdateHandler()

    try:
        LOG.setLevel(logging.INFO)
        LOG.handlers.insert(0, new_handler)
        LOG.propagate = False

        patch_game(input_path, configuration)
        if new_handler.count < total_logs:
            status_update(1, "Done")

    finally:
        LOG.removeHandler(new_handler)
        LOG.propagate = True
