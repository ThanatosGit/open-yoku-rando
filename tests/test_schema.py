from pathlib import Path

import pytest

from open_yoku_rando import yoku_patcher

configuration_jsons = sorted(Path(__file__).parent.joinpath("test_files", "patcher_files").glob("*.json"))


@pytest.mark.parametrize("configuration_path", configuration_jsons, ids=lambda path: path.name)
def test_schema_validation(test_files_dir, configuration_path):
    configuration = test_files_dir.read_json(configuration_path)

    yoku_patcher.validate(configuration)
