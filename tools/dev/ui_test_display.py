"""표시형 검사에 공통 CLI의 창 시작 위치를 전달한다."""

from pathlib import Path
import subprocess
import sys


def ui_test_window_args():
    """macOS에서 실행할 때마다 현재 모니터의 시작 위치를 읽는다."""
    if sys.platform != 'darwin':
        return []
    return subprocess.check_output(
        ['/bin/zsh', str(Path(__file__).with_name('ui-test-window-args.sh'))],
        text=True).splitlines()
