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

const ITEM_PRESETS = Object.freeze({
  tab: { icon: "+", iconClass: "blank", title: "New tab", detail: "Ready to browse" },
  chat: { icon: "✦", iconClass: "chat", title: "New chat", detail: "Conversation ready" },
  note: { icon: "N", iconClass: "note", title: "Untitled note", detail: "Local note" },
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
  const createMenu = requireElement(shell, '[data-region="create-menu"]');
  const createMenuTitle = requireElement(createMenu, '[data-field="create-menu-title"]');
  let newTabSequence = 0;
  let newGroupSequence = 0;
  let createTargetGroup;
  let createMenuTrigger;

  function tabs() {
    return elements(tabList, '[data-action="select-tab"]');
  }

  function updateCount() {
    tabCount.textContent = String(tabs().length);

    elements(tabList, "[data-group]").forEach((group) => {
      const groupCount = group.querySelector('[data-field="group-count"]');
      if (groupCount) {
        groupCount.textContent = String(elements(group, '[data-action="select-tab"]').length);
      }
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

  function closeCreateMenu({ restoreFocus = false } = {}) {
    if (createMenu.hidden) return;
    createMenu.hidden = true;
    if (!isSidebarOpen()) setSidebarOpen(true);
    createMenuTrigger?.setAttribute("aria-expanded", "false");
    if (restoreFocus) createMenuTrigger?.focus();
    createMenuTrigger = undefined;
    createTargetGroup = undefined;
  }

  function openCreateMenu(trigger) {
    if (!createMenu.hidden && createMenuTrigger === trigger) {
      closeCreateMenu({ restoreFocus: true });
      return;
    }

    createMenuTrigger?.setAttribute("aria-expanded", "false");
    createMenuTrigger = trigger;
    createTargetGroup = trigger.dataset.targetGroup;
    const group = createTargetGroup
      ? tabList.querySelector(`[data-group="${createTargetGroup}"]`)
      : null;
    const groupName = group?.querySelector(".tab-group-toggle > span:nth-child(2)")?.textContent;
    createMenuTitle.textContent = groupName ? `Add to ${groupName}` : "Create new";
    requireElement(createMenu, '[data-action="create-group"]').hidden = Boolean(groupName);
    trigger.setAttribute("aria-expanded", "true");
    createMenu.hidden = false;

    const sidebarRect = requireElement(shell, ".tab-sidebar").getBoundingClientRect();
    const triggerRect = trigger.getBoundingClientRect();
    const top = Math.min(triggerRect.bottom - sidebarRect.top + 4, sidebarRect.height - createMenu.offsetHeight - 10);
    createMenu.style.top = `${Math.max(8, top)}px`;
    createMenu.querySelector('[role="menuitem"]')?.focus();
  }

  function createItem(kind = "tab", targetGroupName) {
    const preset = ITEM_PRESETS[kind] ?? ITEM_PRESETS.tab;
    newTabSequence += 1;
    const tabId = `${kind}-${newTabSequence}`;
    const tab = document.createElement("button");
    tab.className = "tab-item";
    tab.type = "button";
    tab.setAttribute("role", "tab");
    tab.setAttribute("aria-selected", "false");
    tab.dataset.tab = tabId;
    tab.dataset.action = "select-tab";
    tab.dataset.kind = kind;
    tab.innerHTML = `
      <span class="favicon ${preset.iconClass}">${preset.icon}</span>
      <span class="tab-copy"><strong>${preset.title}</strong><small>${preset.detail}</small></span>
      <span class="tab-side"><span class="tab-close" aria-hidden="true" data-action="close-tab">×</span></span>
    `;
    const targetGroup = targetGroupName
      ? tabList.querySelector(`[data-group="${targetGroupName}"]`)
      : null;
    const targetContainer = targetGroup?.querySelector(".tab-group-items") ?? looseTabs;

    if (targetGroup) {
      targetGroup.dataset.groupState = "open";
      setBooleanAttribute(requireElement(targetGroup, '[data-action="toggle-tab-group"]'), "aria-expanded", true);
    }

    targetContainer.append(tab);
    updateCount();
    selectTab(tab);

    if (isSidebarOpen()) {
      tab.focus();
      tab.scrollIntoView({ block: "nearest" });
    }

    closeCreateMenu();
    return tab;
  }

  function createTab(targetGroupName) {
    return createItem("tab", targetGroupName);
  }

  function createGroup() {
    newGroupSequence += 1;
    const groupId = `collection-${newGroupSequence}`;
    const group = document.createElement("section");
    group.className = "tab-group is-new-group";
    group.dataset.group = groupId;
    group.dataset.groupState = "open";
    group.setAttribute("role", "presentation");
    group.innerHTML = `
      <div class="tab-group-heading">
        <button class="tab-group-toggle" type="button" aria-controls="group-${groupId}-items" aria-expanded="true" data-action="toggle-tab-group">
          <span class="group-mark" aria-hidden="true"></span>
          <span class="group-name"><input type="text" value="New group" aria-label="그룹 이름" /></span>
          <small data-field="group-count">0</small>
          <svg viewBox="0 0 16 16" aria-hidden="true"><path d="m4 6 4 4 4-4" /></svg>
        </button>
        <button class="group-add-tab" type="button" aria-label="새 그룹에 항목 추가" aria-controls="sidebar-create-menu" aria-expanded="false" title="그룹에 항목 추가" data-action="toggle-create-menu" data-target-group="${groupId}">
          <svg viewBox="0 0 18 18" aria-hidden="true"><path d="M9 4v10M4 9h10" /></svg>
        </button>
      </div>
      <div class="tab-group-items" id="group-${groupId}-items" role="presentation"></div>
    `;
    tabList.append(group);
    updateCount();
    closeCreateMenu();

    const input = requireElement(group, ".group-name input");
    input.focus();
    input.select();
    const commitName = () => {
      const name = input.value.trim() || `Group ${newGroupSequence}`;
      const label = document.createElement("span");
      label.textContent = name;
      input.replaceWith(label);
      requireElement(group, ".group-add-tab").setAttribute("aria-label", `${name} 그룹에 항목 추가`);
    };
    input.addEventListener("blur", commitName, { once: true });
    input.addEventListener("keydown", (event) => {
      if (event.key === "Enter") input.blur();
      if (event.key === "Escape") {
        input.value = `Group ${newGroupSequence}`;
        input.blur();
      }
    });
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
    if (event.target.closest(".group-name input")) {
      return;
    }

    const bookmarksToggle = event.target.closest('[data-action="toggle-bookmarks"]');
    const groupToggle = event.target.closest('[data-action="toggle-tab-group"]');
    const closeButton = event.target.closest('[data-action="close-tab"]');
    const tab = event.target.closest('[data-action="select-tab"]');

    if (bookmarksToggle) {
      const section = bookmarksToggle.closest(".bookmarks-section");
      const isOpen = section.dataset.bookmarksState === "open";
      section.dataset.bookmarksState = isOpen ? "collapsed" : "open";
      setBooleanAttribute(bookmarksToggle, "aria-expanded", !isOpen);
      requireElement(section, ".bookmark-items").hidden = isOpen;
      return;
    }

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
  function showAgentActivity() {
    setSidebarOpen(true);
    const agentGroup = shell.querySelector(".agent-activity-group");
    if (!agentGroup) return;
    agentGroup.dataset.groupState = "open";
    setBooleanAttribute(requireElement(agentGroup, '[data-action="toggle-tab-group"]'), "aria-expanded", true);
    agentGroup.scrollIntoView({ block: "nearest" });
    requireElement(agentGroup, ".agent-task-item").focus();
  }
  shell.addEventListener("workspace:show-agent-request", showAgentActivity);
  shell.addEventListener("workspace:create-item-request", (event) => {
    createItem(event.detail?.kind ?? "tab", event.detail?.targetGroup);
  });
  shell.addEventListener("workspace:create-group-request", () => createGroup());
  shell.addEventListener("workspace:toggle-sidebar-request", toggleSidebar);

  shell.addEventListener("click", (event) => {
    const quickNewTab = event.target.closest('[data-action="quick-new-tab"]');
    const pinnedApp = event.target.closest('[data-action="open-pinned"]');
    const createTrigger = event.target.closest('[data-action="toggle-create-menu"]');
    const createItemButton = event.target.closest('[data-action="create-item"]');
    const createGroupButton = event.target.closest('[data-action="create-group"]');

    if (quickNewTab) {
      createItem("tab");
      return;
    }

    if (pinnedApp) {
      const tab = createItem("tab");
      tab.dataset.kind = "pinned";
      tab.querySelector(".favicon").className = `favicon ${pinnedApp.dataset.iconClass}`;
      tab.querySelector(".favicon").textContent = pinnedApp.dataset.icon;
      tab.querySelector(".tab-copy strong").textContent = pinnedApp.dataset.title;
      tab.querySelector(".tab-copy small").textContent = pinnedApp.dataset.host;
      renderPage(tab);
      return;
    }

    if (createTrigger) {
      openCreateMenu(createTrigger);
      return;
    }

    if (createItemButton) {
      createItem(createItemButton.dataset.kind, createTargetGroup);
      return;
    }

    if (createGroupButton) {
      createGroup();
    }
  });

  document.addEventListener("click", (event) => {
    if (!createMenu.hidden
      && !createMenu.contains(event.target)
      && !event.target.closest('[data-action="toggle-create-menu"]')) {
      closeCreateMenu();
    }
  });

  shell.addEventListener("workspace:select-tab", (event) => selectTab(event.detail.tabId));

  function handleKeydown(event) {
    if (event.key === "Escape" && !createMenu.hidden) {
      event.preventDefault();
      closeCreateMenu({ restoreFocus: true });
      return true;
    }

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
      createItem("tab");
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
