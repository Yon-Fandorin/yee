"""Negative controls against retained real trials; never rewrites source evidence.

Run: python test-live-aside-handoff-evidence.py --s08 DIR --s10 DIR --s12 DIR
These are opt-in evidence checks, not network/model reruns or fabricated native traces.
"""
import argparse
import copy
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

s=importlib.util.spec_from_file_location('inspection',Path(__file__).with_name('inspect-aside-handoff-trial.py'))
inspection=importlib.util.module_from_spec(s);s.loader.exec_module(inspection)

class EvidenceTests(unittest.TestCase):
    def inspect(self,key,change=None,change_lines=None):
        original_read=inspection.read;original_lines=inspection.lines
        def read(path):
            value=copy.deepcopy(original_read(path))
            return change(path,value) if change else value
        def lines(path):
            value=copy.deepcopy(original_lines(path))
            return change_lines(path,value) if change_lines else value
        # Output generation is disabled: all original records remain untouched.
        with patch.object(inspection,'read',read),patch.object(inspection,'lines',lines),patch.object(Path,'write_text',return_value=0):
            return inspection.inspect(PATHS[key])

    def test_actual_success_and_actual_violations_remain_distinct(self):
        self.assertTrue(self.inspect('s10')['success'])
        if APPROVED:
            self.assertTrue(self.inspect('s08')['success'])
            self.assertTrue(self.inspect('s12')['success'])
        else:
            self.assertIn('scope_or_unsupported_API_requires_review',self.inspect('s08')['failures'])
            self.assertIn('browser_read_before_new_document_permission',self.inspect('s12')['failures'])

    def test_inventory_without_recorded_authorization_is_rejected(self):
        def change(p,v):
            if p.name=='manifest.json' and p.parent.name=='model':
                v['aside_owned_tab_scope']['tab_inventory_metadata_authorized']=False
            return v
        self.assertIn('scope_or_unsupported_API_requires_review',self.inspect('s08',change)['failures'])

    def test_wrong_request_reply_is_rejected(self):
        def change(p,v):
            if p.name.endswith('.reply.json'):v['request_id']='wrong-question'
            return v
        with self.assertRaisesRegex(ValueError,'identity or ordering'):self.inspect('s08',change)

    def test_model_visible_result_tampering_rejected(self):
        def change(p,v):
            if p.name=='stdout.jsonl':
                next(e for e in v if e.get('role')=='tool')['content']+=' UNRECORDED_OUTPUT'
            return v
        with self.assertRaisesRegex(ValueError,'differs from MCP'):self.inspect('s10',change_lines=change)

    def test_wrong_final_active_tab_does_not_pass(self):
        def change(p,v):
            if p.name=='native-post-model.json':v['observation']['matches']=False
            return v
        self.assertIn('native-post-model',self.inspect('s10',change)['failures'])

    def test_unrelated_manual_review_cannot_authorize_evidence(self):
        def change(p,v):
            if p.name=='manual-review.json':v['model_stdout_sha256']='different-run'
            return v
        self.assertIn('hash_bound_original_code_review_required',self.inspect('s10',change)['failures'])

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--metadata-approved',action='store_true',help='expect the new three approved trials to pass; preserve legacy failure expectations by default')
    for key in ('s08','s10','s12'):p.add_argument('--'+key,type=Path,required=True)
    args,rest=p.parse_known_args();PATHS=vars(args);APPROVED=args.metadata_approved
    unittest.main(argv=['test-live-aside-handoff-evidence.py',*rest])
