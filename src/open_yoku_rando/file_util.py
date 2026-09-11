import os
from pathlib import Path


def atomic_write_bytes(path: Path, data: bytes) -> None:
    """Writes through a temporary file, so a crash or a full disk never leaves a half written file behind."""
    temp_path = path.with_name(path.name + ".open-yoku-rando-tmp")
    try:
        with temp_path.open("wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        temp_path.replace(path)
    finally:
        temp_path.unlink(missing_ok=True)
