# Yee community filter data

The uAssets originals under `uAssets/` are GPL-3.0, with upstream notices intact.
`sources.json` records their exact revision, URLs and checksums. The original
Brave uBlock modules and redirect resources under `uBlock/` are recorded in
`scriptlet-sources.json`; their GPL-3.0-or-later and individual notices remain
intact. No upstream file was edited. Generated filters and JavaScript resources
retain the upstream licenses.

The independently authored `build_filter_pack.py`, `preprocess_filters.py` and
`build_scriptlet_resources.mjs`
are intentionally provided under the BSD license in `LICENSE-builder`.
To reproduce the distributed data from the extracted source archive:

```sh
python3 build_filter_pack.py data OUTPUT_DIRECTORY
```

Python 3 and Node.js 22 or newer are required. No npm installation, network
download or Yee checkout is required. The compiler imports the original
registered functions, serializes their source, dependencies and aliases, and
marks `requiresTrust` resources with the uBO permission bit. It includes
supported scriptlet calls and redirects. Unsupported extended cosmetic actions,
parameterized redirects and Brave's excluded `google-ima-dai.js` are omitted.
`selection-report.json` records the selection and omissions.
The package does not contain the Yee engine adapter, renderer code, YouTube
implementation or other private browser sources. The external JavaScript
program uses standard web APIs and ordinary ABP/uBO resource interfaces. It is
interpreted separately, without Yee-specific JavaScript bindings or combining
it with Yee JavaScript source. The public resource compiler also accompanies it.

To modify the data, edit the inputs and update their SHA-256 fields in
`data/sources.json` or `data/scriptlet-sources.json`, then rebuild. A modified pack can be supplied with
`--yee-community-filter-dir=ABSOLUTE_OUTPUT_DIRECTORY`. No browser rebuild is
needed. Packs are loaded at process startup; restart to apply a replacement.
Checksums ensure pack consistency; they are not cryptographic signatures. This
startup option selects trusted executable JavaScript, so use trusted local packs.
Keep original attribution, provide the modified source and mark your changes
when redistributing, as required by the data license.
