import unittest
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
from yee_browser_named import resolve


class NamedTests(unittest.TestCase):
    def response(self, lines):
        return {'ok': True, 'document': 'doc-1', 'truncated': False,
                'snapshot': 'page @doc-1 rev=1\n' + lines}

    def test_unique_control_returns_document_bound_ref(self):
        self.assertEqual(resolve(self.response('+@doc-1_4 field "Name" value=""\n'),
                                 'field', 'Name'), ('doc-1', 'doc-1_4'))

    def test_batch_checkbox_error_preserves_rejection_and_explains_direct_targeting(self):
        with self.assertRaisesRegex(ValueError, 'batch click requires a button'):
            resolve(self.response('+@doc-1_4 checkbox "In stock only"\n'), 'button', 'In stock only')

    def test_named_click_checkbox_and_cross_role_ambiguity(self):
        checkbox = '+@doc-1_4 checkbox "In stock only" checked\n'
        self.assertEqual(resolve(self.response(checkbox), 'click', 'In stock only'),
                         ('doc-1', 'doc-1_4'))
        for lines in (checkbox + '+@doc-1_5 button "In stock only"\n',
                      checkbox.replace(' checked', ' disabled'),
                      '+@doc-1_4 link "In stock only"\n'):
            with self.assertRaises(ValueError):
                resolve(self.response(lines), 'click', 'In stock only')

    def test_duplicates_missing_wrong_role_disabled_secret_rejected(self):
        for lines in ('', '+@doc-1_4 button "Name"\n',
                      '+@doc-1_4 field "Name"\n+@doc-1_5 field "Name"\n',
                      '+@doc-1_4 field "Name" disabled\n',
                      '+@doc-1_4 field "Name" value=<redacted>\n',
                      '+@other_4 field "Name"\n'):
            with self.assertRaises(ValueError):
                resolve(self.response(lines), 'field', 'Name')

    def test_incomplete_error_and_delta_rejected(self):
        for change in ({'ok': False}, {'truncated': True}, {'document': None},
                       {'snapshot': 'page @doc-1 rev=2 delta\n+@doc-1_4 field "Name"\n'}):
            value = self.response('+@doc-1_4 field "Name"\n')
            value.update(change)
            with self.assertRaises(ValueError):
                resolve(value, 'field', 'Name')

    def test_no_fuzzy_or_truncated_name_matching(self):
        for name in ('', 'Nam', 'name', 'Name…', 'Name\n', 'N' * 200):
            with self.assertRaises(ValueError):
                resolve(self.response('+@doc-1_4 field "Name"\n'), 'field', name)

    def test_quoted_name_and_role_are_exact(self):
        self.assertEqual(resolve(self.response('+@doc-1_2 button "Save \\"locally\\""\n'),
                                 'button', 'Save "locally"'), ('doc-1', 'doc-1_2'))

    def exercise_transport(self, observation, expected_commands, action_ok=True, argv=None):
        with tempfile.TemporaryDirectory() as directory:
            bridge = Path(directory)
            requests, errors = [], []
            stop = threading.Event()
            def peer():
                seen = set()
                try:
                    while not stop.wait(0.002):
                        try:
                            request = json.loads((bridge / 'request.json').read_text())
                        except FileNotFoundError:
                            continue
                        if request['id'] in seen:
                            continue
                        seen.add(request['id'])
                        requests.append(request)
                        # Another client cannot acquire the bridge during either
                        # dispatch. The same CLI run owns it through resolution.
                        with (bridge / 'request.lock').open('a') as lock:
                            try:
                                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                            except BlockingIOError:
                                pass
                            else:
                                raise AssertionError('request lock was released')
                        response = dict(observation) if request['command'] == 'observe' else {
                            'ok': action_ok, 'document': 'doc-1', 'revision': 2,
                            'truncated': False, 'snapshot': 'page @doc-1 rev=2 delta\n~@doc-1_4 field "Name" value="Cedar"\n'}
                        if not action_ok and request['command'] != 'observe':
                            response['error'] = 'user_cancelled'
                        response['id'] = request['id']
                        response.setdefault('revision', 1)
                        temporary = bridge / 'peer.tmp'
                        temporary.write_text(json.dumps(response))
                        os.replace(temporary, bridge / 'response.json')
                except Exception as exc:
                    errors.append(exc)
            thread = threading.Thread(target=peer)
            thread.start()
            try:
                result = subprocess.run([sys.executable, str(Path(__file__).with_name('yee-browser.py')),
                    '--bridge', directory, '--timeout', '2', '--compact'] +
                    (argv or ['fill-named', 'Name', 'Cedar']), capture_output=True, text=True, timeout=5)
            finally:
                stop.set()
                thread.join(2)
            self.assertFalse(errors, errors)
            self.assertFalse(thread.is_alive())
            self.assertEqual([r['command'] for r in requests], expected_commands)
            if len(requests) == 2:
                self.assertTrue(requests[0]['full'])
                if requests[1]['command'] == 'batch':
                    self.assertEqual(requests[1]['actions'], [
                        {'command':'fill','ref':'doc-1_4','value':'Cedar'},
                        {'command':'click','ref':'doc-1_7'}])
                else:
                    self.assertEqual(requests[1]['ref'], 'doc-1_4')
                    self.assertEqual(requests[1]['value'], 'Cedar')
                self.assertEqual(requests[1]['baseline_document'], 'doc-1')
            return result

    def test_transport_observes_resolves_then_dispatches_bound_ref(self):
        result = self.exercise_transport(self.response('+@doc-1_4 field "Name" value=""\n'),
                                         ['observe', 'fill'])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(result.stdout.splitlines()), 1)
        self.assertIn('Cedar', json.loads(result.stdout)['snapshot'])

    def test_transport_ambiguity_and_truncation_never_mutate(self):
        duplicate = self.response('+@doc-1_4 field "Name"\n+@doc-1_5 field "Name"\n')
        truncated = {**self.response('+@doc-1_4 field "Name"\n'), 'truncated': True}
        for response in (duplicate, truncated):
            self.assertEqual(self.exercise_transport(response, ['observe']).returncode, 2)

    def test_transport_observation_error_and_action_denial_propagate(self):
        result = self.exercise_transport({'ok': False, 'error': 'not_attached'}, ['observe'])
        self.assertEqual(result.returncode, 1)
        self.assertEqual(json.loads(result.stdout)['error'], 'not_attached')
        result = self.exercise_transport(self.response('+@doc-1_4 field "Name"\n'),
                                         ['observe', 'fill'], action_ok=False)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(json.loads(result.stdout)['error'], 'user_cancelled')

    def test_batch_transport_one_observation_one_native_batch_and_denial(self):
        response = self.response('+@doc-1_4 field "Name"\n+@doc-1_7 button "Save locally"\n')
        argv = ['batch-named','[["fill","Name","Cedar"],["click","Save locally"]]']
        for accepted in (True, False):
            result = self.exercise_transport(response,['observe','batch'],accepted,argv)
            self.assertEqual(result.returncode,0 if accepted else 1,result.stderr)
        response['snapshot'] += '+@doc-1_8 button "Save locally"\n'
        result = self.exercise_transport(response,['observe'],argv=argv)
        self.assertEqual(result.returncode,2)

    def test_invalid_names_fail_before_any_native_observation(self):
        for name in ('', 'N'*161, 'Name\n', 'Name\x00', 'Name…'):
            for argv in (['fill-named',name,'Cedar'],
                         ['batch-named',json.dumps([['fill',name,'Cedar']])]):
                if argv[0] == 'fill-named' and '\x00' in name:
                    continue  # OS argv cannot carry NUL; JSON batch input can.
                result = self.exercise_transport({},[],argv=argv)
                self.assertEqual(result.returncode,2)


if __name__ == '__main__':
    unittest.main()
