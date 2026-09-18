import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PATH = Path(__file__).with_name('check-kimi-trial-config.py')
spec = importlib.util.spec_from_file_location('preflight', PATH)
preflight = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preflight)


class PreflightTests(unittest.TestCase):
    def config(self):
        return {'thinking': {'enabled': True}, 'models': {
            preflight.MODEL_ALIAS: {'provider': 'managed:kimi-code',
                'model': 'kimi-for-coding',
                'capabilities': ['thinking', 'always_thinking', 'tool_use']}},
            'providers': {'managed:kimi-code': {'type': 'kimi',
                'base_url': 'https://api.kimi.com/coding/v1',
                'oauth': {'storage': 'file', 'key': 'SECRET_CANARY'}}}}

    def test_config_match_never_implies_live_authentication_or_trial_readiness(self):
        result = preflight.inspect(self.config())
        self.assertTrue(result['configuration_matches_declared_alias'])
        self.assertFalse(result['model_version_verified'])
        self.assertNotIn('configuration_matches_k27_requirements', result)
        for key in ('authentication_verified', 'runtime_model_identity_verified',
                    'isolated_tool_policy_verified', 'trial_ready'):
            self.assertFalse(result[key])
        self.assertNotIn('SECRET_CANARY', json.dumps(result))

    def test_thinking_off_missing_or_truthy_nonboolean_fails(self):
        for thinking in ({}, {'enabled': False}, {'enabled': 'true'}, {'enabled': 1}, None):
            config = self.config(); config['thinking'] = thinking
            self.assertIn('thinking_must_be_explicitly_enabled',
                          preflight.inspect(config)['reasons'])

    def test_recipient_overrides_and_credentials_are_not_echoed(self):
        for field, value in [('base_url', 'https://api.kimi.com.evil.test/SECRET_CANARY'),
                             ('base_url', 'https://SECRET_CANARY@api.kimi.com/coding/v1'),
                             ('custom_headers', {'X': 'SECRET_CANARY'}),
                             ('oauth', {'storage': 'file', 'key': 'SECRET_CANARY',
                                        'oauth_host': 'https://SECRET_CANARY.test'})]:
            config = self.config(); config['providers']['managed:kimi-code'][field] = value
            result = preflight.inspect(config)
            self.assertFalse(result['configuration_matches_declared_alias'])
            self.assertNotIn('SECRET_CANARY', json.dumps(result))

    def test_wrong_model_missing_capability_and_ambiguous_auth_fail(self):
        base = self.config()
        mutations = [(['models', preflight.MODEL_ALIAS, 'model'], 'k3'),
                     (['models', preflight.MODEL_ALIAS, 'capabilities'], ['tool_use']),
                     (['providers', 'managed:kimi-code', 'api_key'], 'SECRET_CANARY'),
                     (['providers', 'managed:kimi-code', 'oauth'], None)]
        for path, value in mutations:
            config = copy.deepcopy(base); target = config
            for key in path[:-1]: target = target[key]
            target[path[-1]] = value
            self.assertFalse(preflight.inspect(config)['configuration_matches_declared_alias'])

    def test_invalid_toml_does_not_leak_source_or_traceback(self):
        with tempfile.TemporaryDirectory() as root:
            p = Path(root)/'config.toml'; p.write_text('api_key = SECRET_CANARY')
            result = subprocess.run([sys.executable, str(PATH), str(p)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stderr, '')
            self.assertNotIn('SECRET_CANARY', result.stdout)
            self.assertFalse(json.loads(result.stdout)['trial_ready'])


if __name__ == '__main__':
    unittest.main()
