"""Python 검사가 공통 zsh의 빌드 루트·출력 선택을 재사용하게 한다."""

from pathlib import Path
import subprocess


def selected_yee_browser_bin():
    """부작용 전에 두 선택 가드를 확인하고 선택한 Yee 실행 파일 경로를 반환한다."""
    result = subprocess.run(
        ['zsh', '-c', 'source "$1"; require_yee_build_root_selection; '
         'require_yee_output_selection; print -r -- "$YEE_BROWSER_BIN"',
         'yee-browser-path', str(Path(__file__).with_name('common.zsh'))],
        stdout=subprocess.PIPE, text=True)
    if result.returncode:
        raise SystemExit(result.returncode)
    return Path(result.stdout.rstrip('\n'))
