// Copyright 2026 The Yee Authors
// SPDX-License-Identifier: BSD-3-Clause
// Public resource compiler. Original GPL modules remain separate, unmodified
// inputs, and their source accompanies the generated JavaScript resources.
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const root = path.resolve(process.argv[2]);
const { builtinScriptlets } = await import(pathToFileURL(
  path.join(root, 'uBlock/src/js/resources/scriptlets.js')));
const { default: redirects } = await import(pathToFileURL(
  path.join(root, 'uBlock/src/js/redirect-resources.js')));
const mime = new Map([
  ['.js', 'application/javascript'], ['.json', 'application/json'],
  ['.css', 'text/css'], ['.html', 'text/html'], ['.txt', 'text/plain'],
  ['.xml', 'text/xml'], ['.gif', 'image/gif'], ['.png', 'image/png'],
  ['.mp3', 'audio/mp3'], ['.mp4', 'video/mp4'],
]);
const resources = [];
for (const [name, properties] of redirects) {
  // Same exclusions as adblock-rust/Brave's resource assembly: parameterized
  // redirects are unsupported; Brave deliberately omits google-ima-dai.js.
  if (properties.params || name === 'google-ima-dai.js') continue;
  const type = name === 'empty' ? 'text/plain' : mime.get(path.extname(name));
  if (!type) throw new Error(`Unknown MIME type: ${name}`);
  const alias = properties.alias ?? [];
  resources.push({ name, aliases: typeof alias === 'string' ? [alias] : alias,
    kind: { mime: type }, dependencies: [], permission: 0,
    content: (await readFile(path.join(root, 'uBlock/src/web_accessible_resources', name))).toString('base64') });
}
for (const entry of builtinScriptlets) {
  resources.push({ name: entry.name, aliases: entry.aliases ?? [],
    scriptlet: true,
    kind: { mime: 'application/javascript' },
    content: Buffer.from(entry.fn.toString()).toString('base64'),
    dependencies: entry.dependencies ?? [], permission: entry.requiresTrust ? 1 : 0 });
}
const braveMetadata = JSON.parse(await readFile(path.join(root, 'brave-resources/metadata.json'), 'utf8'));
for (const entry of braveMetadata) {
  resources.push({ name: entry.name, aliases: entry.aliases ?? [],
    kind: entry.kind, brave_resource: true, dependencies: [], permission: 2,
    content: (await readFile(path.join(root, 'brave-resources/resources', entry.resourcePath))).toString('base64') });
}
const names = new Set(resources.map(x => x.name));
const identifiers = new Set();
for (const resource of resources) {
  for (const name of [resource.name, ...resource.aliases]) {
    if (identifiers.has(name)) throw new Error(`Duplicate resource identifier: ${name}`);
    identifiers.add(name);
  }
  for (const name of resource.dependencies) {
    if (!names.has(name)) throw new Error(`Missing canonical dependency: ${name}`);
  }
}
process.stdout.write(JSON.stringify(resources) + '\n');
