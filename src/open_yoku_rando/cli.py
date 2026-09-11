import argparse
import json
import logging
import logging.config
import time
from pathlib import Path

from open_yoku_rando import yoku_patcher
from open_yoku_rando.logger import LOG


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Writes a Randovania seed into a Yoku's Island Express install, to be started from the game's "
                    "menu. It also installs the DLL that makes that menu entry work.",
    )
    parser.add_argument("--input-path", required=True, type=Path,
                        help="Path to the Yoku's Island Express install folder.")
    parser.add_argument("--input-json", required=True, type=Path, help="Path to the configuration json.")
    parser.add_argument("-q", "--quiet", action="store_true", help="Only log warnings and errors.")

    return parser


def setup_logging(level: int) -> None:
    handlers = {
        'default': {
            'level': level,
            'formatter': 'default',
            'class': 'logging.StreamHandler',
            'stream': 'ext://sys.stdout',
        },
    }
    logging.config.dictConfig({
        'version': 1,
        'formatters': {
            'default': {
                'format': '[%(asctime)s] [%(levelname)s] [%(name)s] %(funcName)s: %(message)s',
            }
        },
        'handlers': handlers,
        'disable_existing_loggers': False,
        'root': {
            'level': level,
            'handlers': list(handlers.keys()),
        },
    })


def main(argv: list[str] | None = None) -> None:
    parser = create_parser()
    args = parser.parse_args(argv)
    setup_logging(logging.WARNING if args.quiet else logging.DEBUG)

    _run_patch(args)


def _run_patch(args: argparse.Namespace) -> None:
    with args.input_json.open(encoding="utf-8") as f:
        configuration = json.load(f)

    start = time.time()
    yoku_patcher.patch_game(args.input_path, configuration)
    end = time.time()
    LOG.info("Patcher took %.03f seconds", end - start)
