#!/usr/bin/env python3
"""Merges the Maven layouts the release jobs publish (publishToFlowRepo) into one and checks it.

Each job publishes a disjoint set of artifacts, so a path two inputs both hold must be byte-equal
(maven-metadata files aside, which carry a timestamp). The merged layout must hold every required
artifact at --version, and every variant the KMP root module names (available-at) must be there.

  flow_merge_repos.py --version 0.156.1-flow.1 --out merged parts/flow-repo-macos parts/...
"""

import argparse
import filecmp
import json
import pathlib
import shutil
import sys

GROUP_PATH = pathlib.Path('org/jetbrains/skiko')

# What Flow's app takes: the KMP root and the JVM API, the desktop runtimes, the Apple klibs.
REQUIRED = [
    'skiko',
    'skiko-awt',
    'skiko-awt-runtime',
    'skiko-awt-runtime-macos-arm64',
    'skiko-awt-runtime-macos-x64',
    'skiko-awt-runtime-windows-x64',
    'skiko-awt-runtime-linux-x64',
    'skiko-macosarm64',
    'skiko-iosarm64',
    'skiko-iossimulatorarm64',
]


def merge(parts, out):
    for part in parts:
        for src in sorted(p for p in part.rglob('*') if p.is_file()):
            rel = src.relative_to(part)
            dst = out / rel
            if dst.exists():
                if filecmp.cmp(src, dst, shallow=False):
                    continue
                if rel.name.startswith('maven-metadata'):
                    continue
                sys.exit(f'error: {rel} differs between inputs ({part} and an earlier one)')
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)


def check(out, version, required, allow_missing_variants):
    errors = []
    group = out / GROUP_PATH
    if not group.is_dir():
        sys.exit(f'error: {GROUP_PATH} is missing from the merged layout')

    for artifact in required:
        if not (group / artifact / version / f'{artifact}-{version}.pom').is_file():
            errors.append(f'required {artifact}:{version} is missing')

    for artifact_dir in sorted(p for p in group.iterdir() if p.is_dir()):
        versions = sorted(p.name for p in artifact_dir.iterdir() if p.is_dir())
        if versions != [version]:
            errors.append(f'{artifact_dir.name} has versions {versions}, expected [{version}]')

    root_module = group / 'skiko' / version / f'skiko-{version}.module'
    if root_module.is_file():
        for variant in json.loads(root_module.read_text()).get('variants', []):
            available_at = variant.get('available-at')
            if not available_at:
                continue
            target = (root_module.parent / available_at['url']).resolve()
            if not target.is_file():
                message = f"root module variant {variant['name']} points at missing {available_at['module']}"
                if allow_missing_variants:
                    print(f'warning: {message}')
                else:
                    errors.append(message)

    if errors:
        sys.exit('error: ' + '\nerror: '.join(errors))
    artifacts = sorted(p.name for p in group.iterdir() if p.is_dir())
    print(f'{len(artifacts)} artifacts at {version}: ' + ', '.join(artifacts))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--version', required=True)
    parser.add_argument('--out', required=True, type=pathlib.Path)
    parser.add_argument('--require', nargs='*', default=REQUIRED,
                        help='artifact ids that must be present (default: what the app takes)')
    parser.add_argument('--allow-missing-variants', action='store_true',
                        help='warn instead of failing when the root module names a missing klib (partial bundles)')
    parser.add_argument('parts', nargs='+', type=pathlib.Path)
    args = parser.parse_args()

    if args.out.exists():
        sys.exit(f'error: {args.out} exists')
    args.out.mkdir(parents=True)
    merge(args.parts, args.out)
    check(args.out, args.version, args.require, args.allow_missing_variants)


if __name__ == '__main__':
    main()
