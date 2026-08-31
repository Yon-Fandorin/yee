import { elements, requireElement, setBooleanAttribute } from "./dom.js";

const VIEW_NAMES = Object.freeze(["root", "context", "browser-tools", "memory"]);

export function createSidebarFooterController(shell) {
  const footer = requireElement(shell, '[data-region="sidebar-footer"]');
  const trigger = requireElement(footer, '[data-action="toggle-footer-menu"]');
  const menu = requireElement(footer, '[data-footer-surface="menu"]');
  const views = elements(menu, "[data-footer-view]");
  const contextChoices = elements(menu, '[data-action="select-context"]');
  const memorySwitch = requireElement(menu, '[data-action="toggle-memory"]');
  const toast = requireElement(shell, '[data-region="demo-toast"]');
  let activeView = "root";
  let toastTimer = 0;
  let toastHideTimer = 0;

  function updateFields(selector, value) {
    elements(shell, selector).forEach((field) => {
      field.textContent = value;
    });
  }

  function showToast(message) {
    window.clearTimeout(toastTimer);
    window.clearTimeout(toastHideTimer);
    toast.textContent = message;
    toast.hidden = false;
    requestAnimationFrame(() => toast.classList.add("is-visible"));
    toastTimer = window.setTimeout(() => {
      toast.classList.remove("is-visible");
      toastHideTimer = window.setTimeout(() => {
        toast.hidden = true;
      }, 180);
    }, 1800);
  }

  function showView(viewName = "root") {
    const nextView = VIEW_NAMES.includes(viewName) ? viewName : "root";
    views.forEach((view) => {
      view.hidden = view.dataset.footerView !== nextView;
    });
    activeView = nextView;
    menu.dataset.activeView = nextView;
  }

  function isOpen() {
    return !menu.hidden;
  }

  function close({ restoreFocus = false } = {}) {
    menu.hidden = true;
    setBooleanAttribute(trigger, "aria-expanded", false);
    showView("root");

    if (restoreFocus) {
      trigger.focus();
    }
  }

  function open(viewName = "root") {
    shell.dispatchEvent(new CustomEvent("yee:surface-open", {
      detail: { source: "sidebar-footer" },
    }));
    showView(viewName);
    menu.hidden = false;
    setBooleanAttribute(trigger, "aria-expanded", true);
  }

  function toggle() {
    if (isOpen()) {
      close({ restoreFocus: true });
    } else {
      open("root");
    }
  }

  function setMemoryEnabled(enabled) {
    setBooleanAttribute(memorySwitch, "aria-checked", enabled);
    updateFields('[data-field="memory-status"]', enabled ? "On · ›" : "Paused · ›");
    updateFields('[data-field="memory-count"]', enabled
      ? "12 saved memories"
      : "Memory is paused");
  }

  function selectContext(choice) {
    const { memory, tenant, tenantShort, tenantMark, workspace } = choice.dataset;

    contextChoices.forEach((candidate) => {
      const selected = candidate === choice;
      candidate.classList.toggle("is-current", selected);
      setBooleanAttribute(candidate, "aria-checked", selected);
    });
    updateFields('[data-field="tenant-name"]', tenant);
    elements(shell, '[data-field="tenant-mark"]').forEach((mark) => {
      mark.textContent = tenantMark;
      mark.classList.toggle("northstar", tenantMark === "N");
      mark.classList.toggle("acme", tenantMark === "A");
    });
    updateFields('[data-field="workspace-name"]', workspace);
    updateFields('[data-field="hub-context"]', `${tenantShort} / ${workspace}`);
    updateFields('[data-field="memory-context"]', `${workspace} workspace`);
    setMemoryEnabled(memory !== "paused");
    trigger.setAttribute("aria-label", `${tenant} 테넌트, ${workspace} 워크스페이스 메뉴`);
    trigger.title = `${tenant} / ${workspace} workspace`;
    close({ restoreFocus: true });
    showToast(`${tenant} / ${workspace} is now the active context.`);
  }

  function handleKeydown(event) {
    if (event.key !== "Escape" || !isOpen()) {
      return false;
    }

    event.preventDefault();
    if (activeView !== "root") {
      showView("root");
    } else {
      close({ restoreFocus: true });
    }
    return true;
  }

  trigger.addEventListener("click", toggle);

  elements(menu, '[data-action="open-footer-view"]').forEach((control) => {
    control.addEventListener("click", () => showView(control.dataset.view));
  });

  contextChoices.forEach((choice) => {
    choice.addEventListener("click", () => selectContext(choice));
  });

  memorySwitch.addEventListener("click", () => {
    const enabled = memorySwitch.getAttribute("aria-checked") !== "true";
    setMemoryEnabled(enabled);
    showToast(enabled ? "Workspace memory is on." : "Workspace memory is paused.");
  });

  elements(menu, "[data-demo-message]").forEach((control) => {
    control.addEventListener("click", () => {
      showToast(control.dataset.demoMessage);
      close({ restoreFocus: true });
    });
  });

  shell.addEventListener("yee:surface-open", (event) => {
    if (event.detail?.source !== "sidebar-footer") {
      close();
    }
  });

  document.addEventListener("pointerdown", (event) => {
    if (isOpen() && !footer.contains(event.target)) {
      close();
    }
  });

  return Object.freeze({ close, handleKeydown, isOpen, open });
}
