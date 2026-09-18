"""Root-only synthetic viewport setup using measured window-relative geometry."""
import json
import math
import time
from urllib.parse import urlsplit

CHECK = '/private/tmp/yee-owned-check-native-url-20260913-v3'
RESIZE = '/private/tmp/yee-resize-aside-owned-window'
MEASURE = '/private/tmp/yee-measure-owned-aside-chrome-20260913'
SIDEBAR = '/private/tmp/yee-drag-owned-aside-sidebar-20260913-v24'
POSITION = '/private/tmp/yee-position-owned-aside-window-20260913-v24'


def drag_once(url, delta, run):
    command = [SIDEBAR, url, str(delta)]
    try:
        return run(command)
    except RuntimeError as error:
        # The legacy helper rejects a one-pixel rounded result. Preserve that
        # failed acknowledgement, then require an independent state check.
        # This never sends another mouse action.
        operation = getattr(error, 'operation', {})
        if operation.get('command') != command or operation.get('returncode') != 1:
            raise
        try:
            reported = json.loads(operation['stdout'])
            actual = reported['after_handle_x'] - reported['before_handle_x']
            valid = (reported['expected_url'] == url and reported['requested_delta'] == delta
                     and math.isfinite(actual) and abs(actual - delta) <= 1)
        except (KeyError, TypeError, ValueError):
            raise error
        if not valid:
            raise
        return {'failed_acknowledgement': operation, 'settlement_required': True}


def prepare(url, run, *, timeout=4, interval=.05):
    parsed = urlsplit(url)
    if (parsed.scheme, parsed.hostname, parsed.port) != ('http', '127.0.0.1', 8787):
        raise ValueError('Only authorized localhost fixture URLs are supported')
    before = run([CHECK, url, 'at.studio.AsideBrowser'])
    if not before.get('matches') or not before.get('app_frontmost'):
        raise ValueError('Owned active native URL required before layout input')
    pid = before['pid']
    evidence = {'expected_url': url, 'pid': pid, 'before': before}
    # macOS clamps an oversized window at its existing screen origin. Move the
    # owned fixture window into the work area before requesting the target size.
    evidence['position'] = run([POSITION, url, '0', '50'])
    deadline = time.monotonic() + timeout
    probes = 0
    while True:
        chrome = run([MEASURE, url])
        probes += 1
        if chrome.get('pid') != pid or chrome.get('expected_url') != url:
            raise ValueError('Native layout ownership changed; input stopped')
        if abs(chrome['window_x']) <= 1 and abs(chrome['window_y'] - 50) <= 1:
            break
        if time.monotonic() >= deadline:
            raise ValueError('Position did not settle; request was not repeated: ' + json.dumps(chrome, sort_keys=True))
        time.sleep(interval)
    evidence['resize'] = run([RESIZE, url, '1665', '942', 'absolute'])
    deadline = time.monotonic() + timeout
    previous_geometry = None
    resize_corrections = 0
    while True:
        chrome = run([MEASURE, url])
        probes += 1
        if chrome.get('pid') != pid or chrome.get('expected_url') != url:
            raise ValueError('Native layout ownership changed; input stopped')
        dimensions = tuple(chrome[k] for k in ('window_x', 'window_y', 'window_width', 'window_height'))
        if 1595 <= chrome['window_width'] <= 1666 and abs(chrome['window_height'] - 942) <= 1 and dimensions == previous_geometry:
            if chrome['window_width'] < 1664 and chrome['window_x'] > 1 and resize_corrections < 3:
                # A changed origin can leave the first size request clamped.
                # Repair changed geometry within a bounded number of steps.
                evidence['clamped_geometry'] = chrome
                evidence['position_correction'] = run([POSITION, url, '0', '50'])
                evidence['resize_after_position_correction'] = run([RESIZE, url, '1665', '942', 'absolute'])
                resize_corrections += 1
                previous_geometry = None
                continue
            break
        previous_geometry = dimensions
        if time.monotonic() >= deadline:
            raise ValueError('Resize did not settle; no further input: ' + json.dumps(chrome, sort_keys=True))
        time.sleep(interval)
    evidence['resized_geometry'] = chrome
    evidence['requested_width_observed_difference'] = 1665 - chrome['window_width']
    evidence['resize_requests'] = 1 + resize_corrections
    # The drag helper reports absolute screen coordinates. The desired sidebar
    # width belongs to its window, whose origin may change when resized.
    target_sidebar = 220 - evidence['requested_width_observed_difference']
    evidence['target_sidebar_relative_x'] = target_sidebar
    delta = target_sidebar - chrome['sidebar_handle_relative_x']
    if abs(delta) > 100:
        raise ValueError('Sidebar correction exceeds bounded native input')
    if abs(delta) > .5:
        evidence['sidebar'] = drag_once(url, delta, run)
    deadline = time.monotonic() + timeout
    while True:
        chrome = run([MEASURE, url])
        probes += 1
        if chrome.get('pid') != pid or chrome.get('expected_url') != url:
            raise ValueError('Native layout ownership changed; input stopped')
        if abs(chrome['sidebar_handle_relative_x'] - target_sidebar) <= 1:
            evidence['normalized_geometry'] = chrome
            evidence['settlement_probes'] = probes
            return evidence
        if time.monotonic() >= deadline:
            raise ValueError('Sidebar did not settle; drag was not repeated: ' + json.dumps(chrome, sort_keys=True))
        time.sleep(interval)


def correct(url, observed, run, *, timeout=4, interval=.05):
    """Use actual owned page measurements; the caller re-verifies all viewports."""
    if not observed or any(t.get('viewport') != observed[0].get('viewport') for t in observed):
        raise ValueError('Consistent owned viewport measurements required')
    view = observed[0]['viewport']
    if view.get('dpr') != 1:
        raise ValueError('Unsupported viewport scale; no layout input')
    dx, dy = 1440 - view['width'], 900 - view['height']
    if abs(dx) > 100 or abs(dy) > 100:
        raise ValueError('Viewport correction exceeds bounded native input')
    before = run([CHECK, url, 'at.studio.AsideBrowser'])
    if not before.get('matches') or not before.get('app_frontmost'):
        raise ValueError('Owned active native URL required before correction')
    chrome = run([MEASURE, url])
    if chrome.get('pid') != before['pid'] or chrome.get('expected_url') != url:
        raise ValueError('Native layout ownership changed; input stopped')
    target_sidebar = chrome['sidebar_handle_relative_x'] - dx
    if not 150 <= target_sidebar <= 300:
        raise ValueError('Corrected sidebar exceeds supported native bounds')
    evidence = {'before': before, 'measured_viewports': observed, 'before_geometry': chrome,
                'required_viewport': {'width': 1440, 'height': 900, 'dpr': 1}}
    if dx:
        evidence['sidebar'] = drag_once(url, -dx, run)
    if dy:
        evidence['height'] = run([RESIZE, url, '0', str(dy)])
    deadline = time.monotonic() + timeout
    while True:
        after = run([MEASURE, url])
        if after.get('pid') != before['pid'] or after.get('expected_url') != url:
            raise ValueError('Native layout ownership changed; input stopped')
        if abs(after['sidebar_handle_relative_x'] - target_sidebar) <= 1 and abs(after['window_height'] - chrome['window_height'] - dy) <= 1:
            evidence['after_geometry'] = after
            return evidence
        if time.monotonic() >= deadline:
            raise ValueError('Viewport correction did not settle; input was not repeated')
        time.sleep(interval)
