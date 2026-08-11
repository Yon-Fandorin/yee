import { readFile } from "node:fs/promises";
import { resolve } from "node:path";

const root = new URL(".", import.meta.url);
const preferencesPath = resolve(new URL("profile-template/Preferences", root).pathname);
const preferences = await readFile(preferencesPath, "utf8").then(JSON.parse);

if (preferences.vertical_tabs?.enabled !== true) {
  throw new Error("vertical_tabs.enabled must be true");
}

if (preferences.vertical_tabs?.uncollapsed_width !== 232) {
  throw new Error("vertical tab width must remain 232px for the pilot");
}

console.log("native pilot profile: valid");
