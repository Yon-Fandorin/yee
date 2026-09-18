import { requireElement } from "./js/dom.js";
import { createLauncherController } from "./js/launcher.js?v=6";
import { createSidebarFooterController } from "./js/sidebar-footer.js?v=6";
import { createTabSidebarController } from "./js/workspace.js?v=12";

const shell = requireElement(document, '[data-ui="browser-shell"]');
const urlParams = new URLSearchParams(window.location.search);
const titlebarDensity = urlParams.get("titlebar");
const tenantShape = urlParams.get("tenant");
const sidebarState = urlParams.get("sidebar");
const requestedOs = urlParams.get("os");
const requestedTheme = urlParams.get("theme");
const requestedSurface = urlParams.get("surface");

const platform = navigator.userAgentData?.platform || navigator.platform || "";
const detectedOs = /mac/i.test(platform)
  ? "mac"
  : /win/i.test(platform)
    ? "windows"
    : "linux";

if (titlebarDensity === "thin" || titlebarDensity === "regular") {
  shell.dataset.titlebarDensity = titlebarDensity;
}

if (["squircle", "offset", "inset"].includes(tenantShape)) {
  shell.dataset.tenantShape = tenantShape;
}

if (sidebarState === "open" || sidebarState === "closed") {
  shell.dataset.sidebarState = sidebarState;
}

shell.dataset.os = ["mac", "windows", "linux"].includes(requestedOs)
  ? requestedOs
  : detectedOs;

if (requestedTheme === "light" || requestedTheme === "dark") {
  shell.dataset.theme = requestedTheme;
}

shell.querySelectorAll('[data-region="create-menu"] [data-kind="tab"] kbd')
  .forEach((shortcut) => {
    shortcut.textContent = shell.dataset.os === "mac" ? "⌘T" : "Ctrl T";
  });

const yeeTrigger = shell.querySelector('[data-action="open-yee-hub"]');
if (yeeTrigger) {
  const label = shell.dataset.os === "mac" ? "⌘K" : "Ctrl K";
  yeeTrigger.setAttribute("aria-label", `에이전트 및 계정 허브 열기, 결과 2개 및 사용량 경고 1개, ${label}`);
  yeeTrigger.title = `허브 열기 · ${label}`;
}

const commandNewTabShortcut = shell.querySelector('[data-command="new-tab"] kbd');
const commandSidebarShortcut = shell.querySelector('[data-command="toggle-sidebar"] kbd');
if (commandNewTabShortcut) {
  commandNewTabShortcut.textContent = shell.dataset.os === "mac" ? "⌘T" : "Ctrl T";
}
if (commandSidebarShortcut) {
  commandSidebarShortcut.textContent = shell.dataset.os === "mac" ? "⌘B" : "Ctrl B";
}

const launcher = createLauncherController(shell);
const sidebarFooter = createSidebarFooterController(shell);
const tabs = createTabSidebarController(shell);

if (requestedSurface === "hub") {
  requestAnimationFrame(() => launcher.open("hub"));
} else if (["root", "context", "browser-tools", "memory"].includes(requestedSurface)) {
  requestAnimationFrame(() => sidebarFooter.open(requestedSurface));
}

shell.querySelector('[data-page="blank"] [data-action="toggle-launcher"]')
  ?.addEventListener("click", () => launcher.open());

document.addEventListener("keydown", (event) => {
  if (sidebarFooter.handleKeydown(event)) {
    return;
  }

  if (launcher.handleKeydown(event)) {
    return;
  }

  tabs.handleKeydown(event);
});
