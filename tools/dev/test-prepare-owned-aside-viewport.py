import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('owned_viewport', Path(__file__).with_name('prepare-owned-aside-viewport.py'))
viewport = importlib.util.module_from_spec(spec)
spec.loader.exec_module(viewport)
URL = 'http://127.0.0.1:8787/?document=example'


class NativeViewport(unittest.TestCase):
    def runner(self, origin=0, delayed=False, changed_pid=False, sidebar=275):
        commands = []
        measurements = 0
        dragged = False
        resized = False
        current_origin = origin
        current_sidebar = sidebar

        def run(command):
            nonlocal measurements, dragged, resized, current_origin, current_sidebar
            commands.append(command)
            if command[0] == viewport.CHECK:
                return {'matches': True, 'app_frontmost': True, 'pid': 42}
            if command[0] == viewport.RESIZE:
                resized = True
                return {'request_accepted': True}
            if command[0] == viewport.POSITION:
                current_origin = 0
                return {'request_accepted': True}
            if command[0] == viewport.SIDEBAR:
                dragged = True
                old = current_sidebar
                current_sidebar += float(command[2])
                return {'before_handle_x': current_origin + old, 'after_handle_x': current_origin + current_sidebar}
            if resized:
                measurements += 1
            return {'pid': 43 if changed_pid else 42, 'expected_url': URL,
                    'window_x': current_origin, 'window_y': 50,
                    'window_width': 1424 if not resized or delayed and measurements == 1 else 1665,
                    'window_height': 942, 'sidebar_handle_relative_x': current_sidebar}
        return run, commands

    def test_sidebar_correction_is_independent_of_window_screen_origin(self):
        for origin in (0, 63, 203, -300):
            with self.subTest(origin=origin):
                run, commands = self.runner(origin)
                result = viewport.prepare(URL, run)
                self.assertEqual(result['normalized_geometry']['sidebar_handle_relative_x'], 220)
                self.assertEqual([c[2] for c in commands if c[0] == viewport.SIDEBAR], ['-55'])
                position = next(i for i, c in enumerate(commands) if c[0] == viewport.POSITION)
                resize = next(i for i, c in enumerate(commands) if c[0] == viewport.RESIZE)
                self.assertLess(position, resize)

    def test_delayed_resize_is_observed_without_repeating_input(self):
        run, commands = self.runner(delayed=True)
        viewport.prepare(URL, run, interval=.001)
        self.assertEqual(sum(c[0] == viewport.RESIZE for c in commands), 1)

    def test_changed_process_stops_before_sidebar_input(self):
        run, commands = self.runner(changed_pid=True)
        with self.assertRaisesRegex(ValueError, 'ownership changed'):
            viewport.prepare(URL, run)
        self.assertFalse(any(c[0] == viewport.SIDEBAR for c in commands))

    def test_excessive_sidebar_delta_stops_before_drag(self):
        run, commands = self.runner(sidebar=450)
        with self.assertRaisesRegex(ValueError, 'exceeds bounded'):
            viewport.prepare(URL, run)
        self.assertFalse(any(c[0] == viewport.SIDEBAR for c in commands))

    def test_clamped_window_correction_uses_actual_page_width(self):
        run, commands = self.runner(origin=25, sidebar=220)
        viewport.correct(URL, [{'viewport': {'width': 1414, 'height': 900, 'dpr': 1}}], run)
        self.assertEqual([c[2] for c in commands if c[0] == viewport.SIDEBAR], ['-26'])
        self.assertFalse(any(c[0] == viewport.RESIZE for c in commands))

    def test_inconsistent_measurements_produce_no_native_input(self):
        run, commands = self.runner()
        with self.assertRaisesRegex(ValueError, 'Consistent'):
            viewport.correct(URL, [{'viewport': {'width': 1414}}, {'viewport': {'width': 1440}}], run)
        self.assertEqual(commands, [])

    def test_excessive_viewport_correction_produces_no_native_input(self):
        run, commands = self.runner()
        with self.assertRaisesRegex(ValueError, 'bounded'):
            viewport.correct(URL, [{'viewport': {'width': 1000, 'height': 900, 'dpr': 1}}], run)
        self.assertEqual(commands, [])

    def test_rounded_drag_acknowledgement_does_not_repeat_mouse_input(self):
        import json
        calls = []
        def run(command):
            calls.append(command)
            error = RuntimeError('one pixel rounded')
            error.operation = {'command': command, 'returncode': 1,
                'stdout': json.dumps({'before_handle_x':311,'after_handle_x':257,
                                      'requested_delta':-55,'expected_url':URL})}
            raise error
        result = viewport.drag_once(URL, -55, run)
        self.assertEqual(len(calls), 1)
        self.assertTrue(result['settlement_required'])
        self.assertEqual(result['failed_acknowledgement']['returncode'], 1)

    def test_wrong_url_in_failed_drag_receipt_is_never_accepted(self):
        import json
        def run(command):
            error = RuntimeError('wrong URL')
            error.operation = {'command':command,'returncode':1,
                'stdout':json.dumps({'before_handle_x':311,'after_handle_x':256,
                                    'requested_delta':-55,'expected_url':'http://other.invalid/'})}
            raise error
        with self.assertRaisesRegex(RuntimeError, 'wrong URL'):
            viewport.drag_once(URL, -55, run)

    def test_real_clamped_geometry_sequence_converges_before_tab_creation(self):
        calls = []
        states = iter([(1631, 33), (1659, 5), (1664, 0)])
        width, origin, sidebar = 1424, 203, 275
        def run(command):
            nonlocal width, origin, sidebar
            calls.append(command)
            if command[0] == viewport.CHECK:
                return {'matches':True,'app_frontmost':True,'pid':42}
            if command[0] == viewport.POSITION:
                origin = 0
                return {'settled':True}
            if command[0] == viewport.RESIZE:
                width, origin = next(states)
                return {'requested_width':1665,'actual_width':width}
            if command[0] == viewport.SIDEBAR:
                sidebar += float(command[2])
                return {'expected_url':URL,'requested_delta':float(command[2])}
            return {'expected_url':URL,'pid':42,'window_x':origin,'window_y':50,
                    'window_width':width,'window_height':942,'sidebar_handle_relative_x':sidebar}
        result = viewport.prepare(URL, run, interval=.001)
        self.assertEqual(result['normalized_geometry']['window_width'], 1664)
        self.assertEqual(result['normalized_geometry']['sidebar_handle_relative_x'], 219)
        self.assertEqual(result['resize_requests'], 3)
        self.assertEqual(sum(c[0] == viewport.SIDEBAR for c in calls), 1)


if __name__ == '__main__':
    unittest.main()
