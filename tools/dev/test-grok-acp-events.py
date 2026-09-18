"""Actual ACP shape and missing/replayed lifecycle failures; no model invocation."""
import copy
import unittest
from grok_acp_events import inspect_prompt


def sample():
    response={'id':3,'result':{'stopReason':'end_turn','_meta':{'sessionId':'sid','requestId':'p','promptId':'p','usage':{'numTurns':2}}}}
    events=[{'direction':'request','value':{'id':3,'method':'session/prompt','params':{'sessionId':'sid'}}}]
    def update(value):events.append({'direction':'response','value':{'method':'session/update','params':{'sessionId':'sid','_meta':{'promptId':'p'},'update':value}}})
    update({'sessionUpdate':'agent_message_chunk','content':{'type':'text','text':'earlier commentary'}})
    update({'sessionUpdate':'tool_call','toolCallId':'t','title':'use_tool','rawInput':{'tool_name':'yee__yee_browser','tool_input':{'action':'observe'}}})
    update({'sessionUpdate':'tool_call_update','toolCallId':'t','status':'completed','rawOutput':{'type':'MCP','server_name':'yee','tool_name':'yee_browser','output':{'OkayOutput':'original feedback'}}})
    update({'sessionUpdate':'agent_message_chunk','content':{'type':'text','text':'{"value":7}'}})
    events.append({'direction':'response','value':response})
    return {'session_id':'sid','response':response},events


class EventsTests(unittest.TestCase):
    def test_actual_original_output_and_final_text_after_tools(self):
        r,e=sample();out=inspect_prompt(r,e)
        self.assertEqual(out['answer'],{'value':7});self.assertEqual(out['calls'][0]['text'],'original feedback')
        self.assertEqual(out['provider_num_turns'],2);self.assertIsNone(out['terminal_failure'])

    def test_wrong_session_missing_or_replayed_lifecycle_rejected(self):
        r,e=sample()
        variants=[]
        a=copy.deepcopy(e);a[2]['value']['params']['sessionId']='foreign';variants.append(a)
        a=copy.deepcopy(e);a.insert(3,copy.deepcopy(a[2]));variants.append(a)
        a=copy.deepcopy(e);a.pop(3);variants.append(a)
        a=copy.deepcopy(e);a.append(copy.deepcopy(a[-1]));variants.append(a)
        for a in variants:
            with self.assertRaises(ValueError):inspect_prompt(r,a)

    def test_missing_or_unrecognized_original_mcp_output_rejected(self):
        r,e=sample()
        for raw in (None,{'type':'Other'},{'type':'MCP','output':{'OtherOutput':'text'}}):
            a=copy.deepcopy(e);a[3]['value']['params']['update']['rawOutput']=raw
            with self.assertRaises(ValueError):inspect_prompt(r,a)

    def test_actual_installed_error_wrapper_preserves_error_and_rejects_flag_mismatch(self):
        r,e=sample()
        raw={'type':'MCP','server_name':'yee','tool_name':'yee_browser',
             'output':{'Error':'original stale_document response'},'is_error':True}
        e[3]['value']['params']['update']['rawOutput']=raw
        out=inspect_prompt(r,e)
        self.assertTrue(out['calls'][0]['tool_error'])
        self.assertEqual(out['calls'][0]['raw_output'],raw)
        raw['is_error']=False
        with self.assertRaises(ValueError):inspect_prompt(r,e)

    def test_fenced_or_duplicate_key_terminal_is_not_rewritten_to_pass(self):
        r,e=sample()
        for text in ('```json\n{"value":7}\n```','{"value":7,"value":8}'):
            a=copy.deepcopy(e);a[4]['value']['params']['update']['content']['text']=text
            self.assertIsNotNone(inspect_prompt(r,a)['terminal_failure'])


if __name__=='__main__':unittest.main()
