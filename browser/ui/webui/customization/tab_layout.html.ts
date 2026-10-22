// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {YeeTabLayoutElement} from './tab_layout.js';

export function getHtml(this: YeeTabLayoutElement) {
  return html`
    <label for="layout">${loadTimeData.getString('yeeTabLayout')}</label>
    <select id="layout" .value="${this.vertical ? 'side' : 'top'}"
        ?disabled="${this.disabled}" @change="${this.onChange_}">
      <option value="top">${loadTimeData.getString('yeeTabLayoutTop')}</option>
      <option value="side">${
      loadTimeData.getString('yeeTabLayoutSide')}</option>
    </select>
  `;
}
