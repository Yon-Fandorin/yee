// Diagnostic attribution only. Source text is matched in memory, never retained.
export function classifyProfileScript(source, url, ownedSources) {
  for (const [name, text] of Object.entries(ownedSources)) {
    if (text && source.includes(text)) return name;
  }
  if (source.includes('window.__yeeReview') || source.includes('window.__yeeScroll') ||
      source.includes('window.__yeeTransition')) return 'collector';
  return /^https?:/.test(url) ? 'page' : 'unresolved';
}

// As in DevTools, reconstruct timestamps, sort sample/time pairs, and attribute
// each sample until the next timestamp. The last uses the mean sample interval.
// Self categories are exclusive. Inclusive function/stack totals overlap.
export function summarizeCPUWindow(profile, scripts, startUs, endUs) {
  if (!(Number.isFinite(startUs) && Number.isFinite(endUs) && endUs >= startUs))
    throw new Error('Invalid CPU window');
  if (profile.samples?.length !== profile.timeDeltas?.length)
    throw new Error('CPU samples need matching time deltas');
  const nodes = new Map(profile.nodes.map(node => [node.id, node]));
  const parents = new Map(), functions = new Map(), selfByOwner = {};
  for (const node of profile.nodes)
    for (const child of node.children ?? []) parents.set(child, node.id);
  const meta = new Map([['(root)', 'runtime'], ['(idle)', 'idle'], ['(program)', 'native-or-unresolved'],
    ['(garbage collector)', 'gc']]);
  const owner = node => meta.get(node.callFrame.functionName) ??
    scripts.get(node.callFrame.scriptId)?.owner ??
    (/^https?:/.test(node.callFrame.url) ? 'page' : 'unresolved');
  const isInjected = category => category.startsWith('yee-') || category.startsWith('brave-');
  const key = node => JSON.stringify([node.callFrame.scriptId,
    node.callFrame.functionName, node.callFrame.lineNumber, node.callFrame.columnNumber]);
  const add = (node, field, ms) => {
    const identity = key(node), row = functions.get(identity) ?? {
      function: node.callFrame.functionName, owner: owner(node),
      url: node.callFrame.url, line: node.callFrame.lineNumber + 1,
      selfMs: 0, inclusiveMs: 0};
    row[field] += ms; functions.set(identity, row);
  };
  let time = profile.startTime, sampledMs = 0, injectedStackMs = 0;
  const ordered = (profile.samples ?? []).map((id, index) => {
    const delta = profile.timeDeltas[index];
    if (!Number.isFinite(delta)) throw new Error('Invalid CPU delta');
    time += delta;
    return {id, time, index};
  }).sort((a, b) => a.time - b.time);
  const mean = ordered.length > 1 ? (ordered.at(-1).time - ordered[0].time) / (ordered.length - 1) :
    profile.endTime - (ordered[0]?.time ?? profile.endTime);
  const reorderedSamples = ordered.filter((sample, index) => sample.index !== index).length;
  for (let i = 0; i < ordered.length; ++i) {
    const sample = ordered[i];
    const until = Math.min(profile.endTime, ordered[i + 1]?.time ?? sample.time + mean);
    const ms = Math.max(0, Math.min(until, endUs) - Math.max(sample.time, startUs)) / 1000;
    if (!ms) continue;
    const leaf = nodes.get(sample.id);
    if (!leaf) throw new Error('Unknown CPU sample node');
    const category = owner(leaf);
    sampledMs += ms; selfByOwner[category] = (selfByOwner[category] ?? 0) + ms;
    add(leaf, 'selfMs', ms);
    let injected = false;
    const visitedNodes = new Set(), visitedFunctions = new Set();
    for (let id = leaf.id; id !== undefined; id = parents.get(id)) {
      if (visitedNodes.has(id)) throw new Error('Cyclic CPU stack');
      visitedNodes.add(id);
      const node = nodes.get(id); if (!node) break;
      if (isInjected(owner(node))) injected = true;
      if (!visitedFunctions.has(key(node))) {
        add(node, 'inclusiveMs', ms); visitedFunctions.add(key(node));
      }
    }
    if (injected) injectedStackMs += ms;
  }
  const rows = [...functions.values()].filter(row => ![...meta.values()].includes(row.owner));
  const injectedRows = rows.filter(row => isInjected(row.owner));
  return {startUs, endUs, windowMs: (endUs - startUs) / 1000,
    sampledMs, selfByOwner, injectedStackMs, reorderedSamples,
    topSelf: [...rows].sort((a, b) => b.selfMs - a.selfMs).slice(0, 25),
    topInclusive: [...rows].sort((a, b) => b.inclusiveMs - a.inclusiveMs).slice(0, 25),
    injectedSelf: [...injectedRows].sort((a, b) => b.selfMs - a.selfMs).slice(0, 25),
    injectedInclusive: [...injectedRows].sort((a, b) => b.inclusiveMs - a.inclusiveMs).slice(0, 25)};
}
