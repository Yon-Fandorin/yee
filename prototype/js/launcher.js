import { elements, requireElement, setBooleanAttribute } from "./dom.js";

const PLACEHOLDERS = Object.freeze({
  browse: "Search the web or enter an address",
  command: "Search tabs, run a command, or ask Yee",
  search: "Search, enter a URL, or run a command",
});

export function createLauncherController(shell) {
  const trigger = requireElement(shell, '[data-action="toggle-launcher"]');
  const popover = requireElement(shell, '[data-region="launcher-popover"]');
  const input = requireElement(popover, "#launcher-input");
  const results = elements(popover, '[data-action="choose-launcher-result"]');
  const emptyState = requireElement(popover, ".launcher-empty");

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

    results.forEach((result) => {
      const tabId = result.dataset.tabTarget;
      const isOpenTab = elements(shell, '[data-action="select-tab"]')
        .some((tab) => tab.dataset.tab === tabId);
      const matchesQuery = !query || result.textContent.toLocaleLowerCase().includes(query);
      result.hidden = !isOpenTab || !matchesQuery;
    });

    const matches = visibleResults();
    emptyState.hidden = matches.length > 0;
    selectResult(matches[0]);
  }

  function chooseResult(result) {
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

    if (restoreFocus) {
      trigger.focus();
    }
  }

  function open(mode = "search") {
    input.placeholder = PLACEHOLDERS[mode] ?? PLACEHOLDERS.search;
    input.value = "";
    filterResults();
    popover.hidden = false;
    setBooleanAttribute(trigger, "aria-expanded", true);
    requestAnimationFrame(() => input.focus());
  }

  function toggle() {
    if (isOpen()) {
      close();
    } else {
      open();
    }
  }

  function handleKeydown(event) {
    const shortcutKey = event.key.toLowerCase();
    const isLauncherShortcut = (event.metaKey || event.ctrlKey)
      && !event.shiftKey
      && (shortcutKey === "k" || shortcutKey === "l");

    if (isLauncherShortcut) {
      event.preventDefault();
      open(shortcutKey === "l" ? "browse" : "command");
      return true;
    }

    if (event.key === "Escape" && isOpen()) {
      event.preventDefault();
      close({ restoreFocus: true });
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

  trigger.addEventListener("click", toggle);
  input.addEventListener("input", filterResults);
  shell.addEventListener("workspace:tabs-changed", () => {
    if (isOpen()) {
      filterResults();
    }
  });

  results.forEach((result) => {
    result.addEventListener("click", () => chooseResult(result));
  });

  document.addEventListener("pointerdown", (event) => {
    const clickedLauncherControl = popover.contains(event.target)
      || trigger.contains(event.target);

    if (isOpen() && !clickedLauncherControl) {
      close();
    }
  });

  return Object.freeze({ close, handleKeydown, isOpen, open, toggle });
}
