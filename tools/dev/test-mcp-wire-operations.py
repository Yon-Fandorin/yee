import base64
import copy
import importlib.util
import json
from pathlib import Path
import unittest

from mcp_wire_operations import derive

spec=importlib.util.spec_from_file_location('audit',Path(__file__).with_name('inspect-comparator-mcp.py'))
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)


def frame(rows, direction, message):
    n=len(rows); identity=str(n)
    rows.extend([
        {'sequence':n+1,'kind':'wire_received','direction':direction,'frame':identity,
         'monotonic_ns':n*100,'base64':base64.b64encode((json.dumps(message)+'\n').encode()).decode()},
        {'sequence':n+2,'kind':'wire_forwarded','direction':direction,'frame':identity,'monotonic_ns':n*100+1}])


class Tests(unittest.TestCase):
    def records(self):
        rows=[]
        frame(rows,'to_server',{'jsonrpc':'2.0','id':1,'method':'tools/call',
              'params':{'name':'repl','arguments':{'code':'synthetic','title':'Check'}}})
        frame(rows,'to_client',{'jsonrpc':'2.0','method':'notifications/tools/list_changed'})
        frame(rows,'to_client',{'jsonrpc':'2.0','id':1,'method':'roots/list'})
        frame(rows,'to_server',{'jsonrpc':'2.0','id':1,'result':{'roots':[]}})
        frame(rows,'to_client',{'jsonrpc':'2.0','id':1,'result':{
              'content':[{'type':'text','text':'first'},{'type':'text','text':'second'}]}})
        return rows

    def test_bidirectional_ids_notification_and_model_text_join(self):
        rows=self.records();before=copy.deepcopy(rows)
        operations,wire=derive(rows)
        self.assertEqual(rows,before)
        self.assertEqual(wire['reverse_requests'],1)
        self.assertEqual(wire['notifications'],1)
        self.assertFalse(operations[1]['result']['isError'])
        events=[{'role':'assistant','tool_calls':[{'id':'a','function':{
            'name':'mcp__aside__repl','arguments':json.dumps(operations[0]['params']['arguments'])}}]},
            {'role':'tool','tool_call_id':'a','content':'firstsecond'}]
        result=audit.inspect_wire(events,rows,model_tool='mcp__aside__repl',recorded_tool='repl')
        self.assertTrue(result['model_call_correlation_verified'])
        self.assertFalse(result['comparison_ready'])
        self.assertEqual(result['mcp_calls'],1)

    def test_incomplete_delivery_or_reply_is_not_success(self):
        rows=self.records()
        for bad in (rows[:-1],rows[:-2],rows[2:]):
            with self.assertRaises(ValueError):derive(bad)

    def test_duplicate_frame_forwarding_or_reply_rejected(self):
        for index,key,value in [(1,'direction','to_client'),(1,'frame','missing'),
                                (2,'frame','0'),(1,'monotonic_ns',-1),(0,'sequence',True)]:
            rows=self.records();rows[index][key]=value
            with self.subTest(index=index,key=key),self.assertRaises(ValueError):derive(rows)
        rows=self.records()
        frame(rows,'to_client',{'jsonrpc':'2.0','id':1,'result':{}})
        with self.assertRaises(ValueError):derive(rows)

    def test_duplicate_json_keys_rejected(self):
        rows=self.records()
        rows[0]['base64']=base64.b64encode(b'{"jsonrpc":"2.0","id":1,"id":2}\n').decode()
        with self.assertRaises(ValueError):derive(rows)

    def test_error_reply_and_unknown_operation_not_recast_as_success(self):
        for method,result in [('tools/call',{'error':{'code':-1,'message':'synthetic'}}),
                              ('resources/read',{'result':{}})]:
            rows=[]
            frame(rows,'to_server',{'jsonrpc':'2.0','id':1,'method':method})
            frame(rows,'to_client',{'jsonrpc':'2.0','id':1,**result})
            with self.assertRaises(ValueError):derive(rows)


if __name__=='__main__':unittest.main()
