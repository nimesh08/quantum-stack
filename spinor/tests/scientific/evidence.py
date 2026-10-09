"""Portable runtime selection and reproducibility metadata for offline audits."""
from __future__ import annotations

import hashlib
import importlib.metadata
import os
from pathlib import Path
import platform
import subprocess

REPO = Path(__file__).resolve().parents[3]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def binary(name: str, explicit: Path | str | None = None) -> Path:
    if explicit:
        value = Path(explicit).expanduser().resolve()
        if value.is_file():
            return value
        raise FileNotFoundError(f"Configured {name} does not exist: {value}")
    suffix = '.exe' if os.name == 'nt' else ''
    cli = {'spinorc': 'spinor', 'phononc': 'phonon', 'photonc': 'photon'}
    base = REPO / (f'build/{cli[name]}/cli' if name in cli else 'build/spinor/tests/owned')
    for directory in (base, *(base / mode for mode in ('Release', 'RelWithDebInfo', 'Debug'))):
        value = directory / (name + suffix)
        if value.is_file():
            return value.resolve()
    raise FileNotFoundError(f"Build {name} or specify its executable path")


def snapshot(executables: dict[str, Path]) -> dict:
    def git(*args: str) -> bytes:
        return subprocess.check_output(['git', *args], cwd=REPO, stderr=subprocess.DEVNULL)
    try:
        diff = git('diff', 'HEAD', '--binary')
        untracked = [p for p in git('ls-files', '-z', '--others', '--exclude-standard').decode('utf-8').split('\0') if p]
        source = {
            'revision': git('rev-parse', 'HEAD').decode().strip(),
            'worktree_diff_sha256': hashlib.sha256(diff).hexdigest(),
            'untracked_files_sha256': {p: sha256(REPO / p) for p in untracked},
            'dirty': bool(diff or untracked),
        }
    except (OSError, subprocess.CalledProcessError):
        source = {'revision': None, 'source_snapshot_unavailable': True}
    return {'source': source, 'binaries': {name: sha256(path) for name, path in executables.items()}}


def environment(packages: tuple[str, ...]) -> dict:
    return {'python': platform.python_version(), 'platform': platform.platform(),
            'dependencies': {name: importlib.metadata.version(name) for name in packages}}
