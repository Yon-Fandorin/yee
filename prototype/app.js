import { requireElement } from "./js/dom.js";
import { createLauncherController } from "./js/launcher.js";
import { createTabSidebarController } from "./js/workspace.js";

const shell = requireElement(document, '[data-ui="browser-shell"]');
const urlParams = new URLSearchParams(window.location.search);
const titlebarDensity = urlParams.get("titlebar");
const tenantShape = urlParams.get("tenant");
const sidebarState = urlParams.get("sidebar");

if (titlebarDensity === "thin" || titlebarDensity === "regular") {
  shell.dataset.titlebarDensity = titlebarDensity;
}

if (["squircle", "offset", "inset"].includes(tenantShape)) {
  shell.dataset.tenantShape = tenantShape;
}

if (sidebarState === "open" || sidebarState === "closed") {
  shell.dataset.sidebarState = sidebarState;
}

const launcher = createLauncherController(shell);
const tabs = createTabSidebarController(shell);

shell.querySelector('[data-page="blank"] [data-action="toggle-launcher"]')
  ?.addEventListener("click", () => launcher.open());

document.addEventListener("keydown", (event) => {
  if (launcher.handleKeydown(event)) {
    return;
  }

  tabs.handleKeydown(event);
});
