import { elements, requireElement, setBooleanAttribute } from "./dom.js";

const PLACEHOLDERS = Object.freeze({
  browse: "Search the web or enter an address",
  hub: "Ask Yee, search tabs, or run a command",
});

const MODE_COPY = Object.freeze({
  browse: {
    badge: "Address",
    dialogLabel: "주소 및 검색",
    resultsLabel: "Suggestions",
  },
  hub: {
    badge: "Y",
    dialogLabel: "Yee Hub",
    resultsLabel: "Open tabs",
  },
});

export function createLauncherController(shell) {
  const trigger = requireElement(shell, '[data-action="toggle-launcher"]');
  const omnibox = requireElement(shell, '[data-region="omnibox"]');
  const yeeTrigger = requireElement(shell, '[data-action="open-yee-hub"]');
  const siteInfoTrigger = requireElement(shell, '[data-action="toggle-site-info"]');
  const siteInfoPopover = requireElement(shell, ".site-info-popover");
  const bookmarkTrigger = requireElement(shell, '[data-action="toggle-page-bookmark"]');
  const popover = requireElement(shell, '[data-region="launcher-popover"]');
  const input = requireElement(popover, "#launcher-input");
  const tabResults = elements(popover, '[data-action="choose-launcher-result"]');
  const commandResults = elements(popover, '[data-action="choose-launcher-command"]');
  const results = [...commandResults, ...tabResults];
  const emptyState = requireElement(popover, ".launcher-empty");
  const commandRegion = requireElement(popover, '[data-region="launcher-command-results"]');
  const modeField = requireElement(popover, '[data-field="launcher-mode"]');
  const resultsLabel = requireElement(popover, '[data-field="launcher-results-label"]');
  let currentMode = "browse";

  function visibleResults() {
    return results.filter((result) => !result.hidden);
  }

  function selectResult(result) {
    results.forEach((candidate) => {
      const selected = candidate === result;
      candidate.classList.toggle("is-selected", selected);

      if (selected) {
        candidate.setAttribute("aria-current", "true");
      } else {
        candidate.removeAttribute("aria-current");
      }
    });

    result?.scrollIntoView({ block: "nearest" });
  }

  function filterResults() {
    const query = input.value.trim().toLocaleLowerCase();
    const normalizedQuery = query.replace(/^https?:\/\//, "").replace(/\/$/, "");

    tabResults.forEach((result) => {
      const tabId = result.dataset.tabTarget;
      const isOpenTab = elements(shell, '[data-action="select-tab"]')
        .some((tab) => tab.dataset.tab === tabId);
      const matchesQuery = !normalizedQuery
        || result.textContent.toLocaleLowerCase().includes(normalizedQuery);
      result.hidden = !isOpenTab || !matchesQuery;
    });

    commandResults.forEach((result) => {
      const matchesQuery = !query || result.textContent.toLocaleLowerCase().includes(query);
      result.hidden = currentMode !== "hub" || !matchesQuery;
    });

    popover.toggleAttribute("data-has-query", currentMode === "hub" && Boolean(query));

    const matches = visibleResults();
    emptyState.textContent = currentMode === "hub"
      ? "No commands or open tabs match."
      : "No address suggestions match.";
    emptyState.hidden = matches.length > 0;
    selectResult(matches[0]);
  }

  function chooseResult(result) {
    const command = result?.dataset.command;

    if (command) {
      close();

      if (command === "new-tab") {
        shell.dispatchEvent(new CustomEvent("workspace:create-item-request", {
          detail: { kind: "tab" },
        }));
      } else if (command === "new-note") {
        shell.dispatchEvent(new CustomEvent("workspace:create-item-request", {
          detail: { kind: "note" },
        }));
      } else if (command === "new-group") {
        shell.dispatchEvent(new CustomEvent("workspace:create-group-request"));
      } else if (command === "toggle-sidebar") {
        shell.dispatchEvent(new CustomEvent("workspace:toggle-sidebar-request"));
      } else if (command === "show-agent") {
        shell.dispatchEvent(new CustomEvent("workspace:show-agent-request"));
      }

      return true;
    }

    const tabId = result?.dataset.tabTarget;

    if (!tabId) {
      return false;
    }

    shell.dispatchEvent(new CustomEvent("workspace:select-tab", {
      detail: { tabId },
    }));
    close({ restoreFocus: true });
    return true;
  }

  function isOpen() {
    return !popover.hidden;
  }

  function close({ restoreFocus = false } = {}) {
    popover.hidden = true;
    input.value = "";
    setBooleanAttribute(trigger, "aria-expanded", false);
    setBooleanAttribute(yeeTrigger, "aria-expanded", false);
    omnibox.dataset.activeMode = "none";

    if (restoreFocus) {
      (currentMode === "hub" ? yeeTrigger : trigger).focus();
    }
  }

  function closeSiteInfo({ restoreFocus = false } = {}) {
    siteInfoPopover.hidden = true;
    setBooleanAttribute(siteInfoTrigger, "aria-expanded", false);

    if (restoreFocus) {
      siteInfoTrigger.focus();
    }
  }

  function toggleSiteInfo() {
    const willOpen = siteInfoPopover.hidden;
    close();
    if (willOpen) {
      shell.dispatchEvent(new CustomEvent("yee:surface-open", {
        detail: { source: "site-info" },
      }));
    }
    siteInfoPopover.hidden = !willOpen;
    setBooleanAttribute(siteInfoTrigger, "aria-expanded", willOpen);
  }

  function toggleBookmark() {
    const isBookmarked = bookmarkTrigger.getAttribute("aria-pressed") === "true";
    close();
    closeSiteInfo();
    setBooleanAttribute(bookmarkTrigger, "aria-pressed", !isBookmarked);
    bookmarkTrigger.setAttribute("aria-label", isBookmarked
      ? "이 페이지 북마크"
      : "이 페이지 북마크에서 삭제");
    bookmarkTrigger.title = isBookmarked ? "이 페이지 북마크" : "북마크에서 삭제";
  }

  function positionBrowsePopover() {
    const triggerBounds = omnibox.getBoundingClientRect();
    const titlebarBounds = requireElement(shell, '[data-region="titlebar"]').getBoundingClientRect();
    popover.style.setProperty("--launcher-anchor-left", `${triggerBounds.left - titlebarBounds.left}px`);
    popover.style.setProperty("--launcher-anchor-width", `${triggerBounds.width}px`);
  }

  function open(mode = "browse") {
    shell.dispatchEvent(new CustomEvent("yee:surface-open", {
      detail: { source: "launcher" },
    }));
    closeSiteInfo();
    currentMode = mode === "hub" ? "hub" : "browse";
    const modeCopy = MODE_COPY[currentMode];
    popover.dataset.launcherMode = currentMode;
    popover.setAttribute("aria-label", modeCopy.dialogLabel);
    modeField.textContent = modeCopy.badge;
    resultsLabel.textContent = modeCopy.resultsLabel;
    commandRegion.hidden = currentMode !== "hub";
    input.placeholder = PLACEHOLDERS[currentMode];
    input.setAttribute("aria-label", currentMode === "hub"
      ? "Ask Yee, search commands and tabs"
      : "Edit address or search");

    if (currentMode === "browse") {
      const address = shell.querySelector('[data-field="launcher-address"]')?.textContent.trim() ?? "";
      input.value = address.includes(".") ? `https://${address}` : address;
      positionBrowsePopover();
    } else {
      input.value = "";
      popover.style.removeProperty("--launcher-anchor-left");
      popover.style.removeProperty("--launcher-anchor-width");
    }

    popover.hidden = false;
    setBooleanAttribute(trigger, "aria-expanded", currentMode === "browse");
    setBooleanAttribute(yeeTrigger, "aria-expanded", currentMode === "hub");
    omnibox.dataset.activeMode = currentMode;
    filterResults();
    requestAnimationFrame(() => {
      input.focus();

      if (currentMode === "browse") {
        input.select();
      }
    });
  }

  function toggle(mode = "browse") {
    if (isOpen()) {
      if (currentMode === mode) {
        close();
      } else {
        open(mode);
      }
    } else {
      open(mode);
    }
  }

  function handleKeydown(event) {
    const shortcutKey = event.key.toLowerCase();
    const isLauncherShortcut = (event.metaKey || event.ctrlKey)
      && !event.shiftKey
      && (shortcutKey === "k" || shortcutKey === "l");

    if (isLauncherShortcut) {
      event.preventDefault();
      open(shortcutKey === "l" ? "browse" : "hub");
      return true;
    }

    if (event.key === "Escape" && isOpen()) {
      event.preventDefault();
      close({ restoreFocus: true });
      return true;
    }

    if (event.key === "Escape" && !siteInfoPopover.hidden) {
      event.preventDefault();
      closeSiteInfo({ restoreFocus: true });
      return true;
    }

    if (isOpen() && (event.key === "ArrowDown" || event.key === "ArrowUp")) {
      const matches = visibleResults();
      event.preventDefault();

      if (matches.length === 0) {
        return true;
      }

      const selected = results.find((result) => result.classList.contains("is-selected"));
      const currentIndex = Math.max(0, matches.indexOf(selected));
      const offset = event.key === "ArrowDown" ? 1 : -1;
      const nextIndex = (currentIndex + offset + matches.length) % matches.length;
      selectResult(matches[nextIndex]);
      return true;
    }

    if (isOpen() && event.key === "Enter") {
      const selected = results.find((result) => result.classList.contains("is-selected"));

      if (selected) {
        event.preventDefault();
        return chooseResult(selected);
      }
    }

    return false;
  }

  trigger.addEventListener("click", () => toggle("browse"));
  yeeTrigger.addEventListener("click", () => toggle("hub"));
  siteInfoTrigger.addEventListener("click", toggleSiteInfo);
  bookmarkTrigger.addEventListener("click", toggleBookmark);
  input.addEventListener("input", filterResults);
  shell.addEventListener("workspace:tabs-changed", () => {
    if (isOpen()) {
      filterResults();
    }
  });
  shell.addEventListener("yee:surface-open", (event) => {
    if (event.detail?.source !== "launcher") {
      close();
    }
  });

  results.forEach((result) => {
    result.addEventListener("click", () => chooseResult(result));
  });

  window.addEventListener("resize", () => {
    if (isOpen() && currentMode === "browse") {
      positionBrowsePopover();
    }
  });

  document.addEventListener("pointerdown", (event) => {
    const clickedLauncherControl = popover.contains(event.target)
      || omnibox.contains(event.target)
      || yeeTrigger.contains(event.target);

    if (isOpen() && !clickedLauncherControl) {
      close();
    }

    if (!siteInfoPopover.hidden && !siteInfoPopover.contains(event.target)
      && !siteInfoTrigger.contains(event.target)) {
      closeSiteInfo();
    }
  });

  return Object.freeze({ close, handleKeydown, isOpen, open, toggle });
}
