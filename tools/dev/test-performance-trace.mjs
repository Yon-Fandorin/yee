#!/usr/bin/env node
import assert from 'node:assert/strict';
import {sanitizeTraceEvent} from './performance_trace.mjs';

assert.deepEqual(sanitizeTraceEvent({name:'ResourceSendRequest',ph:'I',ts:5,
  args:{url:'https://private.test/?token=secret',data:{postData:'private'}}}),
  {name:'ResourceSendRequest',ph:'I',ts:5});
assert.deepEqual(sanitizeTraceEvent({name:'PipelineReporter',ph:'b',ts:8,args:{
  frame_reporter:{state:'STATE_DROPPED',affects_smoothness:true,frame_sequence:4,
    url:'private',unknown:123},scriptSource:'private'}}),
  {name:'PipelineReporter',ph:'b',ts:8,args:{frame_reporter:{
    state:'STATE_DROPPED',affects_smoothness:true,frame_sequence:4}}});
assert.deepEqual(sanitizeTraceEvent({name:'EventLatency',ph:'b',args:{event_latency:{
  event_type:'GESTURE_SCROLL_UPDATE',vsync_interval_ms:8.333,
  has_high_latency:false,event_latency_id:'private',high_latency_stage:'private'}}}),
  {name:'EventLatency',ph:'b',args:{event_latency:{event_type:'GESTURE_SCROLL_UPDATE',
    has_high_latency:false,vsync_interval_ms:8.333}}});
assert.deepEqual(sanitizeTraceEvent({name:'PipelineReporter',args:{frame_reporter:{
  state:'https://private.test',frame_sequence:Infinity,frame_source:{private:1}}}}),
  {name:'PipelineReporter'});
assert.deepEqual(sanitizeTraceEvent({name:'EventLatency',args:{event_latency:{
  event_type:'PRIVATE_ACCOUNT',has_high_latency:'PRIVATE_ACCOUNT'}}}),
  {name:'EventLatency'});
assert.deepEqual(sanitizeTraceEvent({name:'thread_name',ph:'M',args:{name:'CrRendererMain',
  private:'private'}}),{name:'thread_name',ph:'M',args:{name:'CrRendererMain'}});
console.log('Performance trace argument retention checks passed');
