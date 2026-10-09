#!/usr/bin/env python3
"""Export the authoritative audited registry; never regenerate from stale specs.

The 31 YAML files are the reviewed source of truth. Live capabilities belong
in authenticated discovery snapshots. Use --out-dir to copy this registry for
packaging or inspection; the old historical chip_specs table is not replayed.
"""
from __future__ import annotations
import argparse
import pathlib
import shutil

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out-dir', type=pathlib.Path, help='Destination for reviewed chip YAMLs')
    args = parser.parse_args()
    paths = sorted((ROOT / 'spinor/registry/chips').glob('*.yaml'))
    if args.out_dir is None:
        print(f'{len(paths)} audited profiles are already authoritative; use verify_chip_yamls.py or --out-dir PATH.')
        return 0
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for source in paths:
        destination = args.out_dir / source.name
        if source.resolve() != destination.resolve():
            shutil.copyfile(source, destination)
    print(f'Exported {len(paths)} audited profiles.')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
