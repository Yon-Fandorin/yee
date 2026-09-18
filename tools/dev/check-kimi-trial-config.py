#!/usr/bin/env python3
"""Read-only Kimi Code alias configuration preflight; never emits credentials.

This does not authenticate, refresh OAuth, call a model, or declare a trial ready.
The runner must use an isolated profile/home and separately verify runtime routing.
"""
import argparse
import json
from pathlib import Path
import tomllib

MODEL_ALIAS = 'kimi-code/kimi-for-coding'
MODEL_ID = 'kimi-for-coding'
SOURCE = 'https://www.kimi.com/code/docs/en/kimi-code/models.html'


def inspect(config):
    reasons = []
    models = config.get('models', {})
    model = models.get(MODEL_ALIAS, {}) if isinstance(models, dict) else {}
    if not isinstance(model, dict):
        model = {}
    providers = config.get('providers', {})
    provider_name = model.get('provider')
    provider = (providers.get(provider_name, {})
                if isinstance(providers, dict) and isinstance(provider_name, str) else {})
    if not isinstance(provider, dict):
        provider = {}
    if model.get('model') != MODEL_ID:
        reasons.append('target_model_missing_or_mismatched')
    if provider.get('type') != 'kimi':
        reasons.append('official_kimi_provider_type_required')
    # Exact public endpoint only: reject embedded credentials, queries, fragments,
    # lookalike domains, custom ports, or an override redirecting the credential.
    if provider.get('base_url') not in (
            'https://api.kimi.com/coding/v1', 'https://api.kimi.com/coding/v1/'):
        reasons.append('official_kimi_endpoint_required')
    thinking = config.get('thinking', {})
    if not isinstance(thinking, dict) or thinking.get('enabled') is not True:
        reasons.append('thinking_must_be_explicitly_enabled')
    capabilities = model.get('capabilities', [])
    if (not isinstance(capabilities, list)
            or not {'thinking', 'always_thinking', 'tool_use'}.issubset(
                {v for v in capabilities if isinstance(v, str)})):
        reasons.append('expected_tool_and_thinking_capabilities_missing')
    if provider.get('custom_headers'):
        reasons.append('custom_provider_headers_require_separate_audit')
    oauth = provider.get('oauth')
    api_key = provider.get('api_key')
    api_present = isinstance(api_key, str) and bool(api_key.strip())
    oauth_present = isinstance(oauth, dict) and bool(oauth)
    mode = 'none'
    if api_present and oauth_present:
        reasons.append('ambiguous_api_key_and_oauth')
    elif api_present:
        mode = 'api_key'
    elif oauth_present:
        mode = 'oauth'
        if (oauth.get('storage') not in ('file', 'keyring')
                or not isinstance(oauth.get('key'), str) or not oauth['key'].strip()):
            reasons.append('invalid_oauth_reference')
        # Host overrides need an independent recipient audit before authentication.
        if oauth.get('oauth_host'):
            reasons.append('oauth_host_override_requires_separate_audit')
    else:
        reasons.append('credential_reference_missing')
    return {'schema': 'yee.kimi-config-preflight.v2',
            'requested_model': MODEL_ALIAS, 'model_id': MODEL_ID,
            'official_model_mapping_source': SOURCE,
            'configuration_matches_declared_alias': not reasons,
            'model_version_verified': False,
            'model_version_note': 'The provider can upgrade this alias in place; configuration does not prove a fixed model version.',
            'credential_mode': mode, 'reasons': reasons,
            'authentication_verified': False, 'runtime_model_identity_verified': False,
            'isolated_tool_policy_verified': False, 'trial_ready': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    args = parser.parse_args()
    try:
        with args.config.open('rb') as stream:
            raw = stream.read(1024 * 1024 + 1)
        if len(raw) > 1024 * 1024:
            raise ValueError('size')
        result = inspect(tomllib.loads(raw.decode('utf-8')))
    except (OSError, ValueError, UnicodeError):
        # TOML parse errors may contain source fragments. Never print exceptions.
        result = {'schema': 'yee.kimi-config-preflight.v2', 'trial_ready': False,
                  'configuration_matches_declared_alias': False,
                  'reasons': ['configuration_unreadable_or_invalid']}
    print(json.dumps(result, ensure_ascii=False))
    raise SystemExit(0 if result['configuration_matches_declared_alias'] else 2)
