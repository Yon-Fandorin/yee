import importlib.util
import json
from pathlib import Path
import shutil
import unittest
import uuid

spec = importlib.util.spec_from_file_location('seed', Path(__file__).with_name('prepare-agent-test-profile.py'))
seed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(seed)


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.bridge = Path('/private/tmp/yee-agent.' + uuid.uuid4().hex)
        self.bridge.mkdir(mode=0o700)
        self.addCleanup(shutil.rmtree, self.bridge)

    def test_new_profile_contains_only_requested_native_preference(self):
        seed.prepare(self.bridge, 212)
        pref = self.bridge / 'profile/Default/Preferences'
        self.assertEqual(json.loads(pref.read_text()), {'vertical_tabs': {'uncollapsed_width': 212}})
        self.assertEqual(pref.stat().st_mode & 0o777, 0o600)
        with self.assertRaises(FileExistsError):
            seed.prepare(self.bridge, 250)
        self.assertEqual(json.loads(pref.read_text())['vertical_tabs']['uncollapsed_width'], 212)

    def test_invalid_width_or_nonprivate_bridge_does_not_create_profile(self):
        for width in (0, 125, 401, True):
            with self.assertRaises(ValueError):
                seed.prepare(self.bridge, width)
        self.bridge.chmod(0o755)
        with self.assertRaises(ValueError):
            seed.prepare(self.bridge, 212)
        self.assertFalse((self.bridge / 'profile').exists())


if __name__ == '__main__':
    unittest.main()
