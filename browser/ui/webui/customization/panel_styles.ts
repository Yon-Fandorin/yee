// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';

import {getCss} from './panel.css.js';
import {getCss as getAppearanceCss} from './appearance.css.js';

export function getPanelStyles() {
  return loadTimeData.getBoolean('yeeCustomization') ? getCss() : [];
}

export function getAppearanceStyles() {
  return loadTimeData.getBoolean('yeeCustomization') ? getAppearanceCss() : [];
}

// native grid의 열 수도 갱신해 방향키가 실제 행 배치를 따르게 한다.
// CSS 열 수만 바꾸면 화면과 키보드 탐색의 위치가 어긋난다.
export function observeAppearancePalette(element: HTMLElement):
    ResizeObserver|null {
  if (!loadTimeData.getBoolean('yeeCustomization')) {
    return null;
  }
  const picker = element as HTMLElement&{columns: number};
  const observer = new ResizeObserver(entries => {
    const width = entries[0]?.contentRect.width ?? 0;
    if (width > 0) {
      picker.columns = Math.max(3, Math.min(8, Math.floor((width - 24) / 64)));
    }
  });
  observer.observe(picker);
  return observer;
}
