#!/usr/bin/env python3
"""데스크톱 브랜드 묶음의 설치 계약을 검사한다."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests/tooling'))
suite = unittest.defaultTestLoader.loadTestsFromNames([
    'test_brand_assets', 'test_logo_asset_audit', 'test_vector_brand_generation',
])
sys.exit(not unittest.TextTestRunner().run(suite).wasSuccessful())
