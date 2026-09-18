import argparse
import importlib.util
import json
from pathlib import Path
import unittest
from yee_browser_named import parse_batch, resolve_batch

spec = importlib.util.spec_from_file_location('cli', Path(__file__).with_name('yee-browser.py'))
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)


class BatchTests(unittest.TestCase):
    def test_explicit_check_and_long_fill_roundtrip(self):
        from yee_browser_named import parse_ref_batch
        value='가'*1333+'x'
        actions=parse_batch(json.dumps([['fill','Search',value],['check','Stock',False],['click','Apply']]))
        page={'ok':True,'truncated':False,'document':'doc','snapshot':
              'page @doc rev=1\n+@doc_1 field "Search"\n+@doc_2 checkbox "Stock" checked\n+@doc_3 button "Apply"\n'}
        doc,resolved=resolve_batch(page,actions)
        self.assertEqual(doc,'doc');self.assertEqual(resolved[1],{'command':'check','ref':'doc_2','checked':False})
        self.assertEqual(parse_ref_batch(json.dumps(resolved)),resolved)
        self.assertEqual(resolved[0]['value'],value)
        for bad in ('true',1,None,[],{}):
            with self.assertRaises(ValueError):parse_batch(json.dumps([['check','Stock',bad]]))
            with self.assertRaises(ValueError):parse_ref_batch(json.dumps([{'command':'check','ref':'1','checked':bad}]))

    def test_reference_batch_binds_every_target_before_one_native_request(self):
        config = argparse.Namespace(bridge='/private/tmp/test',timeout=1,request_timeout=1,compact=True)
        actions = [{'command':'fill','ref':'1','value':'Cedar'}, {'command':'click','ref':'2'}]
        args = cli.session_command(['--document','doc','batch-ref',json.dumps(actions),'--full'],config)
        request = cli.request_for(args, {'document':'doc','revision':1})
        self.assertEqual(request['command'],'batch')
        self.assertEqual(request['actions'],[{'command':'fill','ref':'doc_1','value':'Cedar'}, {'command':'click','ref':'doc_2'}])
        self.assertTrue(request['full'])
        for baseline in (None, {'document':'other','revision':1}):
            with self.assertRaises(cli.BridgeError):cli.request_for(args,baseline)
        for ref in ('foreign_2','0','doc_0','doc_1'):
            actions[-1]['ref']=ref
            args.actions_json=json.dumps(actions)
            with self.assertRaises(cli.BridgeError):cli.request_for(args,{'document':'doc','revision':1})

    def test_reference_batch_preflight_rejects_malformed_and_nonfinal_click(self):
        config = argparse.Namespace(bridge='/private/tmp/test',timeout=1,request_timeout=1,compact=True)
        for actions in ([],[{'command':'click','ref':'1'},{'command':'fill','ref':'2','value':'x'}],
                        [{'command':'fill','ref':'1','value':False}],
                        [{'command':'click','ref':'1','approved':True}],
                        [{'command':'fill','ref':'1','value':'가'*1334}]):
            with self.assertRaises(cli.BridgeError):
                cli.session_command(['--document','doc','batch-ref',json.dumps(actions)],config)
        with self.assertRaises(cli.BridgeError):
            cli.session_command(['batch-ref','[{"command":"click","ref":"1"}]'],config)

    def test_valid_plan_and_resolution(self):
        actions = parse_batch('[["fill","Name","Cedar"],["click","Save locally"]]')
        response = {'ok':True,'truncated':False,'document':'doc',
                    'snapshot':'page @doc rev=1\n+@doc_1 field "Name" value=""\n+@doc_2 button "Save locally"\n'}
        doc, resolved = resolve_batch(response,actions)
        self.assertEqual(doc,'doc')
        self.assertEqual(resolved,[{'command':'fill','ref':'doc_1','value':'Cedar'},
                                   {'command':'click','ref':'doc_2'}])
        response['snapshot'] += '+@doc_3 button "Save locally"\n'
        with self.assertRaises(ValueError):
            resolve_batch(response,actions)

    def test_reject_invalid_plan(self):
        for value in (None, [], {}, [['click','Save'],['fill','Name','x']],
                      [['fill','Name','x'],['fill','Name','y']],
                      [['fill','Name','x'*4001]], [['navigate','URL']],
                      [['fill','Name',True]], [['fill',str(i),'v'] for i in range(9)]):
            with self.subTest(value=value), self.assertRaises(ValueError):
                parse_batch(json.dumps(value))

    def test_batch_checkbox_rejected_before_native_dispatch(self):
        actions=parse_batch('[["fill","Search","SSD"],["click","In stock only"]]')
        response={'ok':True,'truncated':False,'document':'doc',
                  'snapshot':'page @doc rev=1\n+@doc_1 field "Search"\n+@doc_2 checkbox "In stock only"\n'}
        with self.assertRaises(ValueError):resolve_batch(response,actions)

    def test_session_syntax_preflight_and_request(self):
        config = argparse.Namespace(bridge='/private/tmp/test',timeout=1,request_timeout=1,compact=True)
        args = cli.session_command(['batch-named','[["click","Save"]]'],config)
        self.assertEqual(args.command,'batch-named')
        with self.assertRaises(cli.BridgeError):
            cli.session_command(['batch-named','[["navigate","URL"]]'],config)
        actions = [{'command':'click','ref':'doc_1'}]
        args.command,args.actions = 'batch',actions
        self.assertEqual(cli.request_for(args)['actions'],actions)


if __name__ == '__main__':
    unittest.main()
