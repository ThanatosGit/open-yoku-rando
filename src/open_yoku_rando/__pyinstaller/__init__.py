import os


# Tells PyInstaller where this distribution keeps its hooks; referenced from the entry point in pyproject.toml.
def get_hook_dirs() -> list[str]:
    return [os.path.dirname(__file__)] # noqa: PTH120
