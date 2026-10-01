// Keep only static frame/input enums and numeric counters in diagnostic traces.
// Network URLs, page text, script source and arbitrary event arguments are dropped.
const enums = {
  state: new Set(['STATE_PRESENTED_ALL','STATE_PRESENTED_PARTIAL',
    'STATE_NO_UPDATE_DESIRED','STATE_DROPPED']),
  scroll_state: new Set(['SCROLL_MAIN_THREAD','SCROLL_COMPOSITOR_THREAD','SCROLL_RASTER','SCROLL_NONE']),
  frame_type: new Set(['FORKED','BACKFILL']),
  event_type: new Set(['MOUSE_PRESSED','MOUSE_RELEASED','MOUSE_WHEEL','KEY_PRESSED',
    'KEY_RELEASED','TOUCH_PRESSED','TOUCH_RELEASED','TOUCH_MOVED','GESTURE_SCROLL_BEGIN',
    'GESTURE_SCROLL_UPDATE','GESTURE_SCROLL_END','GESTURE_DOUBLE_TAP','GESTURE_LONG_PRESS',
    'GESTURE_LONG_TAP','GESTURE_SHOW_PRESS','GESTURE_TAP','GESTURE_TAP_CANCEL',
    'GESTURE_TAP_DOWN','GESTURE_TAP_UNCONFIRMED','GESTURE_TWO_FINGER_TAP',
    'FIRST_GESTURE_SCROLL_UPDATE','MOUSE_DRAGGED','GESTURE_PINCH_BEGIN',
    'GESTURE_PINCH_END','GESTURE_PINCH_UPDATE','INERTIAL_GESTURE_SCROLL_UPDATE',
    'MOUSE_MOVED_EVENT','INERTIAL_GESTURE_SCROLL_END']),
};
function fields(source, allowed) {
  if (!source || typeof source !== 'object' || Array.isArray(source)) return undefined;
  const result = {};
  for (const name of allowed) {
    const value = source[name];
    if (enums[name] ? enums[name].has(value) :
        (typeof value === 'boolean' || (typeof value === 'number' && Number.isFinite(value)))) result[name] = value;
  }
  return Object.keys(result).length ? result : undefined;
}
export function sanitizeTraceEvent(event) {
  const {args, ...timing} = event;
  if (event.ph === 'M' && ['thread_name', 'process_name'].includes(event.name)) {
    timing.args = {name: args?.name};
  } else if (event.name === 'PipelineReporter') {
    for (const name of ['frame_reporter', 'chrome_frame_reporter']) {
      const values = fields(args?.[name], ['state', 'scroll_state', 'frame_type',
        'affects_smoothness', 'has_main_animation', 'has_compositor_animation',
        'has_missing_content', 'has_high_latency', 'layer_tree_host_id',
        'frame_sequence', 'frame_source']);
      if (values) (timing.args ??= {})[name] = values;
    }
  } else if (event.name === 'EventLatency') {
    const values = fields(args?.event_latency, ['event_type', 'has_high_latency',
      'is_janky_scrolled_frame', 'vsync_interval_ms']);
    if (values) timing.args = {event_latency: values};
  }
  return timing;
}
