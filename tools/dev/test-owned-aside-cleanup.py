import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('owned_cleanup',Path(__file__).with_name('cleanup-owned-aside-native.py'))
cleanup=importlib.util.module_from_spec(spec);spec.loader.exec_module(cleanup)


class RemovalSettlement(unittest.IsolatedAsyncioTestCase):
    async def test_delayed_registry_can_settle_without_another_close(self):
        snapshots=iter([['owned','protected'],['owned','protected'],['protected']])
        calls=[]
        async def inventory():
            value=next(snapshots);calls.append(value)
            return [{'targetId':v} for v in value]
        self.assertEqual(await cleanup.wait_closed(inventory,'owned',{'protected'},interval=.001),{'protected'})
        self.assertEqual(len(calls),3)

    async def test_missing_protected_tab_stops_before_another_poll(self):
        calls=[]
        async def inventory():
            calls.append(True);return [{'targetId':'owned'}]
        with self.assertRaisesRegex(RuntimeError,'Protected tab'):
            await cleanup.wait_closed(inventory,'owned',{'protected'},interval=.001)
        self.assertEqual(len(calls),1)

    async def test_never_settled_close_has_a_bounded_deadline(self):
        async def inventory():return [{'targetId':'owned'},{'targetId':'protected'}]
        with self.assertRaisesRegex(RuntimeError,'input was not repeated'):
            await cleanup.wait_closed(inventory,'owned',{'protected'},timeout=.005,interval=.001)


class PartialOwnership(unittest.TestCase):
    def records(self,root,indexes=(1,2)):
        case=Path(root)/'aside/S10';case.mkdir(parents=True)
        for i in indexes:
            (case/f'native-owned-{i:02}.json').write_text(json.dumps({'target_id':str(i)*32,'url':f'http://127.0.0.1:8787/?document=D-11-{i}'}))
        return case

    def test_two_bound_tabs_are_recoverable_after_setup_failure(self):
        with tempfile.TemporaryDirectory() as root:
            self.records(root)
            owned,sha=cleanup.load_owned(Path(root),11)
            self.assertEqual(len(owned),2);self.assertEqual(len(sha),64)

    def test_missing_ownership_step_is_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            self.records(root,(1,3))
            with self.assertRaisesRegex(ValueError,'Contiguous'):cleanup.load_owned(Path(root),11)

    def test_unrelated_tab_url_is_rejected_before_any_close(self):
        with tempfile.TemporaryDirectory() as root:
            case=self.records(root,(1,))
            (case/'native-owned-01.json').write_text(json.dumps({'target_id':'1'*32,'url':'https://grok.com/'}))
            with self.assertRaisesRegex(ValueError,'mismatch'):cleanup.load_owned(Path(root),11)

    def test_attempted_unbound_tab_stays_pending_after_bound_tab_cleanup(self):
        spec=importlib.util.spec_from_file_location('pending_cleanup',Path(__file__).with_name('run-grok-persistent-cohort.py'))
        pending=importlib.util.module_from_spec(spec);spec.loader.exec_module(pending)
        with tempfile.TemporaryDirectory() as root:
            case=self.records(root)
            for i in range(1,4):(case/f'native-setup-attempt-{i:02}.json').write_text('{}')
            (Path(root)/'owned-native-cleanup.json').write_text('{"verified":true}')
            self.assertTrue(pending.native_cleanup_pending(Path(root)))

    def test_successful_cleanup_clears_only_fully_bound_attempts(self):
        spec=importlib.util.spec_from_file_location('pending_cleanup_success',Path(__file__).with_name('run-grok-persistent-cohort.py'))
        pending=importlib.util.module_from_spec(spec);spec.loader.exec_module(pending)
        with tempfile.TemporaryDirectory() as root:
            case=self.records(root)
            for i in range(1,3):(case/f'native-setup-attempt-{i:02}.json').write_text('{}')
            proof=Path(root)/'owned-native-cleanup.json';proof.write_text('{"verified":false}')
            self.assertTrue(pending.native_cleanup_pending(Path(root)))
            proof.write_text('{"verified":true}')
            self.assertFalse(pending.native_cleanup_pending(Path(root)))


if __name__=='__main__':unittest.main()
