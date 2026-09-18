export function requireElement(root, selector) {
  const element = root.querySelector(selector);

  if (!element) {
    throw new Error(`Browser shell is missing required element: ${selector}`);
  }

  return element;
}

export function elements(root, selector) {
  return [...root.querySelectorAll(selector)];
}

export function setBooleanAttribute(element, name, value) {
  element.setAttribute(name, String(value));
}
