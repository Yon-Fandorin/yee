#!/usr/bin/env python3
"""브라우저 빌드 없이 제품 이름의 번역 입력을 검사한다."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests/tooling'))
suite = unittest.defaultTestLoader.loadTestsFromName('test_brand_strings')
sys.exit(not unittest.TextTestRunner().run(suite).wasSuccessful())
