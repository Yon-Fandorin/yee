// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getCss} from './tab_layout.css.js';
import {getHtml} from './tab_layout.html.js';
import {TabLayoutHandler, TabLayoutPageCallbackRouter} from './tab_layout.mojom-webui.js';
import type {TabLayoutHandlerRemote} from './tab_layout.mojom-webui.js';

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

  private connection_: {
    handler: TabLayoutHandlerRemote,
    page: TabLayoutPageCallbackRouter,
    listener: number,
  }|null = null;

  override connectedCallback() {
    super.connectedCallback();
    const handler = TabLayoutHandler.getRemote();
    const page = new TabLayoutPageCallbackRouter();
    const listener = page.setTabLayout.addListener(
        (vertical: boolean, enabled: boolean) => {
          this.vertical = vertical;
          this.disabled = !enabled;
        });
    this.connection_ = {handler, page, listener};
    handler.observe(page.$.bindNewPipeAndPassRemote());
  }

  override disconnectedCallback() {
    const connection = this.connection_;
    this.connection_ = null;
    this.disabled = true;
    if (connection) {
      connection.page.removeListener(connection.listener);
      connection.page.$.close();
      connection.handler.$.close();
    }
    super.disconnectedCallback();
  }

  protected onChange_(event: Event) {
    this.connection_?.handler.setTabLayout(
        (event.target as HTMLSelectElement).value === 'side');
  }

}

customElements.define(YeeTabLayoutElement.is, YeeTabLayoutElement);

declare global {
  interface HTMLElementTagNameMap {
    'yee-tab-layout': YeeTabLayoutElement;
  }
}
