"""Branding access for developer clients; protocol identifiers stay independent."""

import importlib.util
from functools import lru_cache
from pathlib import Path
import sys


# Load the shared implementation by its exact path instead of changing sys.path.
_path = Path(__file__).resolve().parents[2] / "tools/overlay/brand_config.py"
_spec = importlib.util.spec_from_file_location("_product_brand_config", _path)
_config = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _config
_spec.loader.exec_module(_config)


@lru_cache(maxsize=1)
def product_name():
    return _config.load_brand(validate_assets=False).name
