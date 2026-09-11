"""
Turns a pickup's `model` into the item id written into the save.

The game builds a bubble's model path out of the item id, so two pickups can only look different by carrying
different ids.
"""

NOTHING_ITEM_ID = "nothing"
DEFAULT_MODEL = NOTHING_ITEM_ID
"""Shipped as `files/items/nothing_x102.sim`: the Randovania icon."""
MODEL_SUFFIX = "_x102.sim"


def game_item_id(item: str, model: str | None = None) -> str:
    """The default model keeps the bare `nothing`, so the common case is one id and one stacking inventory slot."""
    if item != NOTHING_ITEM_ID or model is None or model == DEFAULT_MODEL:
        return item
    return f"{NOTHING_ITEM_ID}_{model}"
