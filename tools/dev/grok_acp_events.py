"""Strict actual ACP prompt/tool/terminal parsing; no synthetic headless transcript."""
import json
from browser_trial_answer import unique_object, invalid_constant


def frame(event):
    value=event['value']
    return value.get('frame',value)


def inspect_prompt(record,events):
    response=record['response'];meta=response['result']['_meta'];sid=record['session_id']
    pid=meta['promptId']
    if meta['sessionId']!=sid or meta['requestId']!=pid:
        raise ValueError('unmatched prompt/session identity')
    requests=[frame(e) for e in events if e['direction']=='request' and
              frame(e).get('method')=='session/prompt' and frame(e).get('id')==response['id']]
    if len(requests)!=1 or requests[0]['params']['sessionId']!=sid:
        raise ValueError('original unique prompt request missing')
    replies=[frame(e) for e in events if e['direction']=='response' and
             'method' not in frame(e) and frame(e).get('id')==response['id']]
    if len(replies)!=1 or replies[0]!=response:
        raise ValueError('original unique prompt response missing')
    calls={};ordered=[];messages=[];last_tool=-1;turn_starts=set()
    for index,event in enumerate(events):
        value=frame(event)
        if event['direction']!='response' or value.get('method')!='session/update':continue
        params=value['params'];update=params['update'];emeta=params.get('_meta',{})
        if params.get('sessionId')!=sid:raise ValueError('foreign session notification')
        if emeta.get('promptId')!=pid:continue
        if emeta.get('turnStartMs') is not None:turn_starts.add(emeta['turnStartMs'])
        kind=update['sessionUpdate']
        if kind=='tool_call':
            identity=update['toolCallId']
            if identity in calls:raise ValueError('duplicate tool call identity')
            name=update.get('_meta',{}).get('x.ai/tool',{}).get('name',update.get('title'))
            arguments=update.get('rawInput')
            if name not in ('search_tool','use_tool') or not isinstance(arguments,dict):
                raise ValueError('unexpected tool or missing original input')
            call={'id':identity,'name':name,'arguments':arguments,'completed':False,
                  'start_event':index,'received_monotonic_ns':event.get('value',{}).get('received_monotonic_ns')}
            calls[identity]=call;ordered.append(call);last_tool=index
        elif kind=='tool_call_update':
            identity=update['toolCallId']
            if identity not in calls:raise ValueError('tool update without original start')
            call=calls[identity]
            if update.get('status') not in ('completed','failed'):continue
            if call['completed']:raise ValueError('duplicate terminal tool update')
            call.update(completed=True,status=update['status'],end_event=index)
            last_tool=index
            if call['name']=='use_tool':
                raw=update.get('rawOutput')
                if not isinstance(raw,dict) or raw.get('type')!='MCP':
                    raise ValueError('original MCP raw output unavailable')
                output=raw.get('output')
                if not isinstance(output,dict) or len(output)!=1:
                    raise ValueError('unrecognized MCP output wrapper')
                variant,text=next(iter(output.items()))
                if variant not in ('OkayOutput','Error') or not isinstance(text,str):
                    raise ValueError('unrecognized MCP text output')
                if raw.get('is_error',False) is not (variant=='Error'):
                    raise ValueError('MCP output variant/error flag mismatch')
                call.update(server=raw['server_name'],tool=raw['tool_name'],text=text,raw_output=raw,
                            tool_error=variant=='Error')
        elif kind=='agent_message_chunk':
            content=update['content']
            if content.get('type')!='text' or not isinstance(content.get('text'),str):
                raise ValueError('non-text terminal message')
            messages.append((index,content['text']))
    if not ordered or any(not c['completed'] for c in ordered):
        raise ValueError('incomplete actual tool lifecycle')
    text=''.join(t for i,t in messages if i>last_tool)
    answer=None;terminal_failure=None
    try:
        answer=json.loads(text,object_pairs_hook=unique_object,parse_constant=invalid_constant)
        if not isinstance(answer,dict):raise ValueError('terminal object required')
    except ValueError as exc:terminal_failure=str(exc)
    return {'session_id':sid,'prompt_id':pid,'rpc_id':response['id'],
            'stop_reason':response['result']['stopReason'],'calls':ordered,
            'observed_turn_starts':len(turn_starts),'provider_num_turns':meta.get('usage',{}).get('numTurns'),
            'answer':answer,'terminal_text':text,'terminal_failure':terminal_failure,
            'original_request_response_verified':True,'actual_tool_lifecycle_verified':True}
