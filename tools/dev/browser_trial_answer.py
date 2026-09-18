"""Shared strict JSON answer policy for recorded browser comparison trials."""
import json

POLICY = 'terminal-json-object-v1'


def unique_object(pairs):
    result={}
    for key,value in pairs:
        if key in result:raise ValueError('duplicate JSON key')
        result[key]=value
    return result


def invalid_constant(value):
    raise ValueError('non-finite JSON number: '+value)


def final_answer(events):
    messages=[e for e in events if e.get('role') in ('assistant','tool','user')]
    if not messages or messages[-1].get('role')!='assistant' or messages[-1].get('tool_calls'):
        raise ValueError('missing terminal assistant answer')
    content=messages[-1].get('content')
    if not isinstance(content,str):raise ValueError('unsupported final answer shape')
    answer=json.loads(content,object_pairs_hook=unique_object,parse_constant=invalid_constant)
    if not isinstance(answer,dict):raise ValueError('expected a final JSON object')
    return answer
