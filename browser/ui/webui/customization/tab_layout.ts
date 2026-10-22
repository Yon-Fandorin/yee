// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getCss} from './tab_layout.css.js';
import {getHtml} from './tab_layout.html.js';

export class YeeTabLayoutElement extends CrLitElement {
  static get is() {
    return 'yee-tab-layout';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {vertical: {type: Boolean}, disabled: {type: Boolean}};
  }

  accessor vertical: boolean = false;
  accessor disabled: boolean = true;

  protected onChange_(event: Event) {
    this.fire('layout-change', {
      vertical: (event.target as HTMLSelectElement).value === 'side',
    });
  }
}

customElements.define(YeeTabLayoutElement.is, YeeTabLayoutElement);

declare global {
  interface HTMLElementTagNameMap {
    'yee-tab-layout': YeeTabLayoutElement;
  }
}
