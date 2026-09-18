# Chromium overlay rules

Root [`../AGENTS.md`](../AGENTS.md) applies.

## Where code goes

| Kind | Location |
| --- | --- |
| Yee visual/product code | `browser/ui/` |
| Chromium hosting glue | `patches/0001-integrate-yee-shell.patch` |
| Product name/icons | `patches/0002-brand-yee-application.patch` |
| Windows proto/python | `patches/0003-fix-windows-protoc-python-aliases.patch` |

Read [`docs/browser-shell-spec.md`](../docs/browser-shell-spec.md) and
[`docs/sidebar/`](../docs/sidebar) before adding or refreshing a UI patch.
Do not copy prototype CSS.

## Glue

A Chromium original file may include a Yee header and call a Yee helper.
It may not inline Yee geometry or teach `TabStripModel` Yee product
names.

If Yee branches start taking over a Chromium function, extract a helper
under `browser/ui/` and leave a short call site.

## Patch regen

After editing Chromium files in `.local-build/chromium/src`:

1. Diff every path that actually changed, including new ones.
2. Write that `git diff` to `patches/0001-integrate-yee-shell.patch`.
3. Drop paths that are identical to upstream again.
4. Sync owned sources with `tools/overlay/install-yee-ui-sources.sh`.
   `build/overlay.json` defines their source/destination bindings.
