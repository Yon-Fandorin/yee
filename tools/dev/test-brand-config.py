#!/usr/bin/env python3
"""Compatibility entry point; implementation lives in tests/."""

from pathlib import Path
import runpy

if __name__ == '__main__':
    runpy.run_path(str(Path(__file__).resolve().parents[2] / 'tests/tooling/test_brand_config.py'), run_name='__main__')
