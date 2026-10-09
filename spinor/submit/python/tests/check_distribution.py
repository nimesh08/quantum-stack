"""Verify a built provider wheel from an isolated install outside the checkout.

Usage: python tests/check_distribution.py path/to/heisenberg_spinor_submit.whl
This performs no provider authentication or network jobs. pip installs only
the wheel's declared Python dependencies.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import venv
import zipfile


def main(wheel: str) -> None:
    wheel_path = Path(wheel).resolve()
    with zipfile.ZipFile(wheel_path) as archive:
        names = archive.namelist()
        chips = [name for name in names if name.startswith("qstack/data/registry/chips/") and name.endswith(".yaml")]
        topologies = [name for name in names if name.startswith("qstack/data/registry/topologies/") and name.endswith(".yaml")]
        assert len(chips) == 31, f"Expected 31 registry profiles in wheel, got {len(chips)}"
        assert topologies, "Target topologies are absent from the wheel"
        assert "qstack/providers/CONTRACTS.md" in names
        assert "qstack/LICENSE" in names
        assert any("/cassettes/" in name and name.endswith(".json") for name in names)
    with tempfile.TemporaryDirectory(prefix="qstack-wheel-check-") as directory:
        temp = Path(directory)
        environment = temp / "environment"
        venv.EnvBuilder(with_pip=True).create(environment)
        python = environment / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        subprocess.run([str(python), "-m", "pip", "install", "--disable-pip-version-check", str(wheel_path)], check=True)
        env = dict(os.environ)
        for name in ("PYTHONPATH", "SPINOR_REGISTRY_ROOT", "QSTACK_CONFIG", "QSTACK_ENV_FILE"):
            env.pop(name, None)
        code = """
from qstack.registry import registry_root, profiles
from qstack.providers import ADAPTERS
from pathlib import Path
import qstack
root = registry_root()
assert root == Path(qstack.__file__).parent / 'data' / 'registry', root
assert len(profiles()) == 31
assert {'ibm', 'google', 'ionq', 'aws', 'azure'} <= set(ADAPTERS)
print('Installed wheel: 31 registry profiles, topology data, and provider routes verified')
"""
        subprocess.run([str(python), "-I", "-c", code], cwd=temp, env=env, check=True)
        subprocess.run([str(python), "-I", "-m", "qstack", "--help"], cwd=temp, env=env, check=True)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_distribution.py <wheel>")
    main(sys.argv[1])
