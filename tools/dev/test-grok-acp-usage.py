import copy
import unittest
from grok_acp_usage import normalize


class UsageTests(unittest.TestCase):
    def prompt(self, identity, input_tokens, output_tokens, last_call):
        usage = {'inputTokens': input_tokens, 'outputTokens': output_tokens,
                 'totalTokens': input_tokens+output_tokens,
                 'cachedReadTokens': 10, 'reasoningTokens': 2,
                 'modelCalls': 2, 'apiDurationMs': 20}
        usage['modelUsage'] = {'grok-4.6-build': dict(usage)}
        return {'response': {'id': identity, 'result': {'stopReason': 'end_turn',
            '_meta': {'sessionId': 'owned', 'requestId': str(identity),
                      'promptId': str(identity), 'modelId': 'grok-4.6',
                      'totalTokens': last_call, 'usage': usage}}}}

    def test_whole_prompt_usage_and_decreasing_second_prompt_are_not_cumulative(self):
        prompts = [self.prompt(1, 100, 20, 30), self.prompt(2, 50, 10, 15)]
        result = normalize(prompts, 'owned')
        self.assertEqual(result['totals']['totalTokens'], 180)
        self.assertEqual(result['totals']['cachedReadTokens'], 20)
        self.assertFalse(result['provider_billing_reconciled'])

    def test_final_call_metadata_is_not_a_missing_usage_fallback(self):
        prompt = self.prompt(1, 100, 20, 30)
        del prompt['response']['result']['_meta']['usage']
        with self.assertRaises(ValueError): normalize([prompt], 'owned')

    def test_replay_and_foreign_session_are_rejected(self):
        prompt = self.prompt(1, 100, 20, 30)
        with self.assertRaises(ValueError): normalize([prompt, copy.deepcopy(prompt)], 'owned')
        with self.assertRaises(ValueError): normalize([prompt], 'other')

    def test_impossible_and_boolean_counters_are_rejected(self):
        for field, value in [('totalTokens', 1), ('cachedReadTokens', 101),
                             ('reasoningTokens', 21), ('inputTokens', True)]:
            prompt = self.prompt(1, 100, 20, 30)
            prompt['response']['result']['_meta']['usage'][field] = value
            with self.assertRaises(ValueError): normalize([prompt], 'owned')

    def test_per_model_totals_must_reconcile(self):
        prompt = self.prompt(1, 100, 20, 30)
        prompt['response']['result']['_meta']['usage']['modelUsage']['grok-4.6-build']['inputTokens'] = 99
        with self.assertRaises(ValueError): normalize([prompt], 'owned')


if __name__ == '__main__': unittest.main()
