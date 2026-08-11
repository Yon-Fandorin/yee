import { elements, requireElement, setBooleanAttribute } from "./dom.js";

const STATIC_PAGES = Object.freeze({
  chromium: {
    address: "chromium.org/developers/design-documents/multi-process-architecture",
    title: "Multi-process architecture",
    page: "document",
  },
  servo: { address: "servo.org/about", title: "Servo embedding", page: "blank" },
  notes: { address: "yee://notes/engine", title: "Engine notes", page: "blank" },
  security: { address: "openreview.net/browser-agent-security", title: "Browser agent security", page: "blank" },
  mojo: { address: "chromium.googlesource.com/mojo", title: "Mojo interfaces", page: "blank" },
});

export function createTabSidebarController(shell) {
  const tabList = requireElement(shell, '[data-region="tab-list"]');
  const looseTabs = requireElement(tabList, '[data-region="loose-tabs"]');
  const tabCount = requireElement(shell, '[data-field="tab-count"]');
  const sidebarToggle = requireElement(shell, '[data-action="toggle-sidebar"]');
  const sidebarEdgeTrigger = requireElement(shell, '[data-action="pin-sidebar-from-edge"]');
  const launcherTitle = requireElement(shell, '[data-field="launcher-title"]');
  const launcherAddress = requireElement(shell, '[data-field="launcher-address"]');
  const documentPage = requireElement(shell, '[data-page="document"]');
  const documentTitle = requireElement(documentPage, '[data-field="page-title"]');
  const blankPage = requireElement(shell, '[data-page="blank"]');
  const blankTitle = requireElement(blankPage, '[data-field="blank-title"]');
  const newTabButtons = elements(shell, '[data-action="new-tab"]');
  let newTabSequence = 0;

  function tabs() {
    return elements(tabList, '[data-action="select-tab"]');
  }

  function updateCount() {
    tabCount.textContent = String(tabs().length);

    elements(tabList, "[data-group]").forEach((group) => {
      const groupCount = requireElement(group, '[data-field="group-count"]');
      groupCount.textContent = String(elements(group, '[data-action="select-tab"]').length);
    });

    shell.dispatchEvent(new CustomEvent("workspace:tabs-changed"));
  }

  function isSidebarOpen() {
    return shell.dataset.sidebarState !== "closed";
  }

  function setSidebarOpen(open) {
    shell.dataset.sidebarState = open ? "open" : "closed";
    setBooleanAttribute(sidebarToggle, "aria-expanded", open);
    setBooleanAttribute(sidebarEdgeTrigger, "aria-expanded", open);
    sidebarToggle.setAttribute("aria-label", open ? "탭 사이드바 숨기기" : "탭 사이드바 보이기");
    sidebarToggle.title = open ? "탭 사이드바 숨기기 · ⌘B" : "탭 사이드바 보이기 · ⌘B";

    if (!open && shell.querySelector(".tab-sidebar:focus-within")) {
      sidebarToggle.focus();
    }
  }

  function toggleSidebar() {
    setSidebarOpen(!isSidebarOpen());
  }

  function pinSidebarFromEdge() {
    setSidebarOpen(true);
    tabs().find((tab) => tab.getAttribute("aria-selected") === "true")?.focus();
  }

  function toggleGroup(group) {
    const groupToggle = requireElement(group, '[data-action="toggle-tab-group"]');
    const open = group.dataset.groupState === "collapsed";
    group.dataset.groupState = open ? "open" : "collapsed";
    setBooleanAttribute(groupToggle, "aria-expanded", open);
  }

  function renderPage(tab) {
    const page = STATIC_PAGES[tab.dataset.tab] ?? {
      address: "Search or enter an address",
      title: tab.querySelector(".tab-copy strong")?.textContent ?? "New tab",
      page: "blank",
    };

    launcherTitle.textContent = page.title;
    launcherAddress.textContent = page.address.split("/")[0];
    documentTitle.textContent = page.title;
    blankTitle.textContent = page.title;
    documentPage.hidden = page.page !== "document";
    blankPage.hidden = page.page === "document";
  }

  function selectTab(tabOrId) {
    const nextTab = typeof tabOrId === "string"
      ? tabs().find((tab) => tab.dataset.tab === tabOrId)
      : tabOrId;

    if (!nextTab) {
      return false;
    }

    tabs().forEach((tab) => {
      const selected = tab === nextTab;
      tab.classList.toggle("is-active", selected);
      setBooleanAttribute(tab, "aria-selected", selected);
      tab.tabIndex = selected ? 0 : -1;
    });

    elements(tabList, "[data-group]").forEach((group) => {
      group.classList.toggle("has-active-tab", group.contains(nextTab));
    });

    renderPage(nextTab);
    return true;
  }

  function createTab() {
    newTabSequence += 1;
    const tabId = `new-tab-${newTabSequence}`;
    const tab = document.createElement("button");
    tab.className = "tab-item";
    tab.type = "button";
    tab.setAttribute("role", "tab");
    tab.setAttribute("aria-selected", "false");
    tab.dataset.tab = tabId;
    tab.dataset.action = "select-tab";
    tab.innerHTML = `
      <span class="favicon blank">+</span>
      <span class="tab-copy"><strong>New tab</strong><small>Ready to browse</small></span>
      <span class="tab-side"><span class="tab-close" aria-hidden="true" data-action="close-tab">×</span></span>
    `;
    looseTabs.append(tab);
    updateCount();
    selectTab(tab);

    if (isSidebarOpen()) {
      tab.focus();
      tab.scrollIntoView({ block: "nearest" });
    }
  }

  function closeTab(tab) {
    const openTabs = tabs();

    if (openTabs.length === 1) {
      return;
    }

    const index = openTabs.indexOf(tab);
    const wasSelected = tab.getAttribute("aria-selected") === "true";
    const replacement = openTabs[index + 1] ?? openTabs[index - 1];
    tab.remove();
    updateCount();

    if (wasSelected && replacement) {
      selectTab(replacement);

      if (isSidebarOpen()) {
        replacement.focus();
      }
    }
  }

  tabList.addEventListener("click", (event) => {
    const groupToggle = event.target.closest('[data-action="toggle-tab-group"]');
    const closeButton = event.target.closest('[data-action="close-tab"]');
    const tab = event.target.closest('[data-action="select-tab"]');

    if (groupToggle) {
      toggleGroup(groupToggle.closest("[data-group]"));
      return;
    }

    if (!tab) {
      return;
    }

    if (closeButton) {
      event.stopPropagation();
      closeTab(tab);
      return;
    }

    selectTab(tab);
  });

  sidebarToggle.addEventListener("click", toggleSidebar);
  sidebarEdgeTrigger.addEventListener("click", pinSidebarFromEdge);
  newTabButtons.forEach((button) => button.addEventListener("click", createTab));

  shell.addEventListener("workspace:select-tab", (event) => selectTab(event.detail.tabId));

  function handleKeydown(event) {
    const isSidebarShortcut = (event.metaKey || event.ctrlKey)
      && !event.shiftKey
      && event.key.toLowerCase() === "b";

    if (isSidebarShortcut) {
      event.preventDefault();
      toggleSidebar();
      return true;
    }

    const isNewTabShortcut = (event.metaKey || event.ctrlKey)
      && !event.shiftKey
      && event.key.toLowerCase() === "t";

    if (isNewTabShortcut) {
      event.preventDefault();
      createTab();
      return true;
    }

    const isCloseTabShortcut = (event.metaKey || event.ctrlKey)
      && !event.shiftKey
      && event.key.toLowerCase() === "w";

    if (isCloseTabShortcut) {
      event.preventDefault();
      const activeTab = tabs().find((tab) => tab.getAttribute("aria-selected") === "true");

      if (activeTab) {
        closeTab(activeTab);
      }

      return true;
    }

    const currentTab = event.target.closest?.('[data-action="select-tab"]');
    const isArrow = event.key === "ArrowDown" || event.key === "ArrowUp";

    if (!currentTab || !isArrow) {
      return false;
    }

    event.preventDefault();
    const openTabs = tabs().filter((tab) => tab.closest("[data-group]")?.dataset.groupState !== "collapsed");
    const offset = event.key === "ArrowDown" ? 1 : -1;
    const nextIndex = (openTabs.indexOf(currentTab) + offset + openTabs.length) % openTabs.length;
    selectTab(openTabs[nextIndex]);
    openTabs[nextIndex].focus();
    return true;
  }

  updateCount();
  setSidebarOpen(isSidebarOpen());
  selectTab("chromium");

  return Object.freeze({ closeTab, createTab, handleKeydown, selectTab, toggleSidebar });
}
