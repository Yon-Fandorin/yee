#!/usr/bin/env python3
"""Overlay ownership, patch preflight, and source sync. Never invokes a build."""

import argparse
from dataclasses import dataclass
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess


REPO_ROOT = Path(__file__).resolve().parents[3]


def relative_path(value):
    if (not isinstance(value, str) or not value or '\\' in value
            or any(c in value for c in ':*?[]\n\r\0')):
        raise ValueError(f'Expected a literal relative path: {value!r}')
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ('', '.', '..') for part in value.split('/')):
        raise ValueError(f'Expected a literal relative path: {value!r}')
    return value


def load_catalog(repo_root=REPO_ROOT):
    catalog = json.loads((Path(repo_root) / 'build/overlay.json').read_text(encoding='utf-8'))
    fields = {'schema_version', 'patches', 'source_roots', 'generated_roots',
              'new_glue', 'ignored_source_names'}
    if not isinstance(catalog, dict) or set(catalog) != fields or catalog['schema_version'] != 2:
        raise ValueError('Unsupported overlay.json schema')
    roles, files = set(), set()
    if not isinstance(catalog['patches'], list) or not catalog['patches']:
        raise ValueError('Expected an ordered patch series')
    for patch in catalog['patches']:
        if (not isinstance(patch, dict) or set(patch) != {'role', 'file', 'strategy'}
                or patch['strategy'] not in ('whole', 'sections')
                or not isinstance(patch['role'], str) or not patch['role']
                or patch['role'] in roles or relative_path(patch['file']) in files):
            raise ValueError('Expected unique patch roles/files and a known strategy')
        roles.add(patch['role'])
        files.add(patch['file'])
    for key in ('generated_roots', 'new_glue', 'ignored_source_names'):
        values = catalog[key]
        if not isinstance(values, list) or any(relative_path(value) != value for value in values):
            raise ValueError(f'Expected relative paths in {key}')
        if len(values) != len(set(values)):
            raise ValueError(f'Duplicate entries in {key}')
    bindings = catalog['source_roots']
    if not isinstance(bindings, list) or not bindings:
        raise ValueError('Expected owned source/destination bindings')
    for binding in bindings:
        if not isinstance(binding, dict) or set(binding) != {'source', 'destination'}:
            raise ValueError('Expected source and destination in every binding')
        relative_path(binding['source'])
        relative_path(binding['destination'])
        if binding['source'].split('/')[0] not in ('browser', 'renderer', 'components', 'third_party'):
            raise ValueError('Expected a product source ownership directory')
    for roots in ([binding['source'] for binding in bindings],
                  [binding['destination'] for binding in bindings] + catalog['generated_roots']):
        for i, root in enumerate(roots):
            if any(root == other or root.startswith(other + '/') or other.startswith(root + '/')
                   for other in roots[i + 1:]):
                raise ValueError('Overlay ownership roots must not overlap')
    if any('/' in name for name in catalog['ignored_source_names']):
        raise ValueError('Ignored source names must be individual path components')
    return catalog


def patch_paths(patch_file):
    text = Path(patch_file).read_text(encoding='utf-8')
    paths = re.findall(r'^diff --git a/(\S+) b/(\S+)\r?$', text, re.MULTILINE)
    if (not paths or text.count('diff --git ') != len(paths)
            or any(old != new for old, new in paths)):
        raise ValueError(f'Expected explicit, unchanged patch paths in {patch_file}')
    names = [relative_path(new) for _, new in paths]
    if len(names) != len(set(names)):
        raise ValueError(f'Duplicate patch sections in {patch_file}')
    return names


def chromium_path(value):
    path = Path(value)
    if not path.is_absolute() or not path.is_dir():
        raise ValueError(f'Expected an absolute Chromium source directory: {value}')
    return path


def git_apply(src, args):
    return subprocess.run(['git', '-C', str(src), 'apply', *args],
                          capture_output=True, text=True)


@dataclass(frozen=True)
class PatchStep:
    patch: Path
    include: str | None
    pending: bool

    @property
    def arguments(self):
        return ([f'--include={self.include}'] if self.include else []) + [str(self.patch)]


def patch_plan(src, catalog, repo_root=REPO_ROOT, role=None):
    """Check every selected section before applying any of them.

    Series entries own disjoint files. Reject overlaps so checking later patches
    against the current checkout cannot silently depend on earlier mutations.
    """
    selected = [p for p in catalog['patches'] if role is None or p['role'] == role]
    if not selected:
        raise ValueError(f'Unknown patch role: {role}')
    plan, owners = [], set()
    for patch in selected:
        patch_file = Path(repo_root) / patch['file']
        paths = patch_paths(patch_file)
        if owners.intersection(paths):
            raise ValueError('Patch series entries must own disjoint files')
        owners.update(paths)
        for include in paths if patch['strategy'] == 'sections' else [None]:
            args = ([f'--include={include}'] if include else []) + [str(patch_file)]
            pending = git_apply(src, ['--reverse', '--check', *args]).returncode != 0
            if pending:
                result = git_apply(src, ['--check', *args])
                if result.returncode:
                    raise RuntimeError(f'Cannot apply {patch_file.name}'
                                       f'{": " + include if include else ""}\n{result.stderr.strip()}')
            plan.append(PatchStep(patch_file, include, pending))
    return plan


def apply_patch_plan(src, plan, check_only=False):
    for step in plan:
        label = step.patch.name + (f': {step.include}' if step.include else '')
        if not step.pending:
            print(f'Already applied: {label}')
        elif check_only:
            print(f'Applicable: {label}')
        else:
            result = git_apply(src, step.arguments)
            if result.returncode:
                raise RuntimeError(result.stderr.strip())
            print(f'Applied: {label}')


def source_plan(src, catalog, repo_root=REPO_ROOT):
    root = Path(repo_root)
    bindings = catalog['source_roots']
    for binding in bindings:
        owned = root / binding['source']
        if not owned.is_dir():
            raise ValueError(f"Missing owned source root: {binding['source']}")
        reject_source_links(root, owned)
        reject_source_links(src, src / binding['destination'])
    families = sorted({binding['source'].split('/')[0] for binding in bindings})
    plan = []
    expected = set()
    for family in families:
        for source in sorted((root / family).rglob('*')):
            parts = source.relative_to(root).parts
            if any(part in catalog['ignored_source_names'] for part in parts):
                continue
            if source.is_symlink():
                raise ValueError(f'Source symlinks are not supported: {source}')
            if not source.is_file():
                continue
            relative = source.relative_to(root).as_posix()
            binding = next((b for b in bindings if relative.startswith(b['source'] + '/')), None)
            if binding is None:
                # Root documentation describes ownership; it is not an overlay input.
                if relative == f'{family}/README.md':
                    continue
                raise ValueError(f'Register the source owner in build/overlay.json: {relative}')
            suffix = source.relative_to(root / binding['source'])
            destination = src / binding['destination'] / suffix
            expected.add(destination)
            reject_source_links(src, destination)
            if not destination.resolve().is_relative_to(src.resolve()):
                raise ValueError(f'Source destination escapes the checkout: {destination}')
            if destination.exists() and not destination.is_file():
                raise ValueError(f'Source destination is not a file: {destination}')
            for parent in destination.parents:
                if parent == src:
                    break
                if parent.exists() and not parent.is_dir():
                    raise ValueError(f'Source destination parent is not a directory: {parent}')
            if not destination.is_file() or source.read_bytes() != destination.read_bytes():
                plan.append((source, destination))
    # Only these catalog roots are owned mirrors. Inspect every deletion before
    # any write; generated roots and Chromium originals are never scanned here.
    for binding in bindings:
        target = src / binding['destination']
        if target.exists() and not target.is_dir():
            raise ValueError(f'Source destination root is not a directory: {target}')
        for destination in sorted(target.rglob('*')):
            if any(part in catalog['ignored_source_names'] for part in destination.relative_to(target).parts):
                continue
            reject_source_links(src, destination)
            if destination.is_file() and destination not in expected:
                plan.append((None, destination))
    return plan


def reject_source_links(root, path):
    for current in (path, *path.parents):
        if current.is_symlink():
            raise ValueError(f'Owned source paths may not traverse links: {current}')
        if current == root:
            break


def sync_sources(plan, check_only=False):
    for source, destination in plan:
        if not check_only:
            if source is None:
                destination.unlink()
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
                destination.chmod(0o644)
    deleted = sum(source is None for source, _ in plan)
    print(f'{"Would sync" if check_only else "Synced"} {len(plan)} Yee overlay files ({deleted} deletions).')


def shell_exclusions(catalog, repo_root=REPO_ROOT):
    roots = [b['destination'] for b in catalog['source_roots']] + catalog['generated_roots']
    paths = [path for patch in catalog['patches'] if patch['role'] != 'shell'
             for path in patch_paths(Path(repo_root) / patch['file'])]
    return [f':(exclude){root}/**' for root in roots] + [f':(exclude){path}' for path in paths]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('patches', 'sources'))
    parser.add_argument('chromium_src', type=chromium_path)
    parser.add_argument('--role', help='Select one patch role (patches only)')
    parser.add_argument('--check', action='store_true', help='Validate and preview without writing')
    args = parser.parse_args()
    try:
        catalog = load_catalog()
        if args.command == 'patches':
            apply_patch_plan(args.chromium_src, patch_plan(args.chromium_src, catalog,
                                                         role=args.role), args.check)
        else:
            if args.role:
                parser.error('--role applies only to patches')
            sync_sources(source_plan(args.chromium_src, catalog), args.check)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(3, f'{error}\n')


if __name__ == '__main__':
    main()
