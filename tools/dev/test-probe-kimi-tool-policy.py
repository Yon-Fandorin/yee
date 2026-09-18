import copy
import base64
import importlib.util
import json
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('probe',Path(__file__).with_name('probe-kimi-tool-policy.py'))
probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)


class PolicyTests(unittest.TestCase):
    def records(self):
        requests=[{'tools':[{'name':'mcp__yee__yee_browser'}]}]*2
        mcp=[{'method':'tools/call','params':{'name':'yee_browser','arguments':{'commands':[['status']]}}}]
        rows=[{'role':'assistant','tool_calls':[
            {'id':'allowed_call','function':{'name':'mcp__yee__yee_browser'}},
            {'id':'forbidden_call','function':{'name':'Bash'}}]},
            {'role':'tool','tool_call_id':'allowed_call','content':'SYNTHETIC_YEE_TOOL_EXECUTED'},
            {'role':'tool','tool_call_id':'forbidden_call','content':'Tool "Bash" not found'}]
        return requests,mcp,rows

    def evaluate(self,requests,mcp,rows,code=0,canary=False):
        return probe.evaluate_policy(requests,mcp,'\n'.join(json.dumps(row) for row in rows),code,canary)

    def test_both_positive_and_negative_execution_evidence_required(self):
        requests,mcp,rows=self.records()
        self.assertTrue(self.evaluate(requests,mcp,rows)['policy_probe_pass'])
        self.assertFalse(self.evaluate(requests,[],rows)['policy_probe_pass'])
        self.assertFalse(self.evaluate([{'tools':[]}]*2,[],rows)['policy_probe_pass'])
        self.assertFalse(self.evaluate(requests,mcp,rows[:-1])['policy_probe_pass'])

    def test_canary_exit_failure_extra_tools_and_duplicate_results_fail(self):
        requests,mcp,rows=self.records()
        self.assertFalse(self.evaluate(requests,mcp,rows,canary=True)['policy_probe_pass'])
        self.assertFalse(self.evaluate(requests,mcp,rows,code=1)['policy_probe_pass'])
        self.assertFalse(self.evaluate(requests,mcp,rows+[rows[-1]])['policy_probe_pass'])
        unsafe=copy.deepcopy(requests);unsafe[0]['tools'].append({'name':'Read'})
        self.assertFalse(self.evaluate(unsafe,mcp,rows)['policy_probe_pass'])

    def test_wrong_mcp_arguments_and_unrelated_error_are_not_denial_proof(self):
        requests,mcp,rows=self.records()
        mcp[0]['params']['arguments']={'commands':[['different']]}
        self.assertFalse(self.evaluate(requests,mcp,rows)['policy_probe_pass'])
        requests,mcp,rows=self.records();rows[-1]['content']='Invalid Bash parameters'
        self.assertFalse(self.evaluate(requests,mcp,rows)['runtime_denial_verified'])

    def test_relay_requires_complete_exact_request_delivery(self):
        message={'jsonrpc':'2.0','id':1,'method':'tools/call','params':{'name':'synthetic'}}
        rows=[{'sequence':1,'kind':'wire_received','frame':'a','direction':'to_server',
               'base64':base64.b64encode((json.dumps(message)+'\n').encode()).decode()},
              {'sequence':2,'kind':'wire_forwarded','frame':'a','direction':'to_server'}]
        events=[{'method':message['method'],'params':message['params']}]
        self.assertTrue(probe.evaluate_relay(rows,events)['verified'])
        self.assertFalse(probe.evaluate_relay(rows[:1],events)['verified'])
        self.assertFalse(probe.evaluate_relay(rows,[])['verified'])
        for key,value in [('frame','other'),('direction','to_client'),('sequence',1)]:
            bad=copy.deepcopy(rows);bad[1][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):probe.evaluate_relay(bad,events)

    def test_aside_identity_and_arguments_are_not_yee_aliases(self):
        requests,mcp,rows=self.records()
        tool,arguments,_=probe.tool_spec('aside')
        requests=[{'tools':[{'name':'mcp__aside__repl'}]}]*2
        mcp[0]['params']={'name':tool,'arguments':arguments}
        rows[0]['tool_calls'][0]['function']['name']='mcp__aside__repl'
        raw='\n'.join(json.dumps(row) for row in rows)
        result=probe.evaluate_policy(requests,mcp,raw,0,False,tool_profile='aside')
        self.assertTrue(result['policy_probe_pass'])
        self.assertEqual(result['tool_profile'],'aside')
        self.assertFalse(probe.evaluate_policy(requests,mcp,raw,0,False)['policy_probe_pass'])
        mcp[0]['params']['arguments']={'commands':[['status']]}
        self.assertFalse(probe.evaluate_policy(requests,mcp,raw,0,False,tool_profile='aside')['policy_probe_pass'])


if __name__=='__main__':unittest.main()
