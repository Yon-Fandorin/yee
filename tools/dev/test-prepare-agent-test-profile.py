import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('seed', Path(__file__).with_name('prepare-agent-test-profile.py'))
seed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(seed)


class ProfileTests(unittest.TestCase):
    def test_new_profile_contains_only_requested_native_preference(self):
        with tempfile.TemporaryDirectory(prefix='yee-agent.', dir='/private/tmp') as directory:
            seed.prepare(Path(directory), 212)
            pref = Path(directory) / 'profile/Default/Preferences'
            self.assertEqual(json.loads(pref.read_text()), {'vertical_tabs': {'uncollapsed_width': 212}})
            self.assertEqual(pref.stat().st_mode & 0o777, 0o600)
            with self.assertRaises(FileExistsError):
                seed.prepare(Path(directory), 250)
            self.assertEqual(json.loads(pref.read_text())['vertical_tabs']['uncollapsed_width'], 212)

    def test_invalid_width_or_nonprivate_bridge_does_not_create_profile(self):
        with tempfile.TemporaryDirectory(prefix='yee-agent.', dir='/private/tmp') as directory:
            for width in (0, 125, 401, True):
                with self.assertRaises(ValueError):
                    seed.prepare(Path(directory), width)
            Path(directory).chmod(0o755)
            with self.assertRaises(ValueError):
                seed.prepare(Path(directory), 212)
            self.assertFalse((Path(directory) / 'profile').exists())


if __name__ == '__main__':
    unittest.main()
