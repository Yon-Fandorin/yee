"""Owner task isolation stops before new grants if release is not proven."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import AsyncMock, patch

spec = importlib.util.spec_from_file_location(
    'persistent_cohort', Path(__file__).with_name('run-grok-persistent-cohort.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class TaskBoundaryTests(unittest.IsolatedAsyncioTestCase):
    async def test_settled_release_and_empty_inventory(self):
        cli = AsyncMock(side_effect=[
            {'ok': True, 'execution_settled': True},
            {'ok': True, 'execution_settled': True, 'tabs': []}])
        with patch.object(runner, 'owner_cli', cli):
            await runner.release_previous_task(
                Path('case'), Path('bridge'), 123, 'task-boundary')
        self.assertEqual([call.args[-1] for call in cli.call_args_list], ['detach', 'tabs'])
        self.assertEqual(
            [call.args[3] for call in cli.call_args_list],
            ['task-boundary-release-previous-task',
             'task-boundary-released-inventory'])

    async def test_unsettled_release_has_no_further_request(self):
        cli = AsyncMock(return_value={'ok': True, 'execution_settled': False})
        with patch.object(runner, 'owner_cli', cli), self.assertRaises(RuntimeError):
            await runner.release_previous_task(Path('case'), Path('bridge'), 123)
        self.assertEqual(cli.await_count, 1)

    async def test_remaining_capability_is_not_filtered_or_retried(self):
        cli = AsyncMock(side_effect=[
            {'ok': True, 'execution_settled': True},
            {'ok': True, 'execution_settled': True, 'tabs': [{'tab': 'old'}]}])
        with patch.object(runner, 'owner_cli', cli), self.assertRaises(RuntimeError):
            await runner.release_previous_task(Path('case'), Path('bridge'), 123)
        self.assertEqual(cli.await_count, 2)

    async def test_missing_or_failed_inventory_is_not_empty(self):
        for inventory in ({'ok': True, 'execution_settled': True},
                          {'ok': False, 'execution_settled': True, 'tabs': []}):
            cli = AsyncMock(side_effect=[{'ok': True, 'execution_settled': True}, inventory])
            with patch.object(runner, 'owner_cli', cli), self.assertRaises(RuntimeError):
                await runner.release_previous_task(Path('case'), Path('bridge'), 123)
            self.assertEqual(cli.await_count, 2)


if __name__ == '__main__':
    unittest.main()
