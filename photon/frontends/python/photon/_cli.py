"""Console launchers for the native compiler binaries shipped in the wheel."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


def _launch(name: str) -> int:
    package = Path(__file__).resolve().parent
    executable = package / "bin" / (name + (".exe" if os.name == "nt" else ""))
    if not executable.is_file():
        raise SystemExit(f"Bundled compiler is missing: {executable}; reinstall heisenberg-photon")
    env = dict(os.environ)
    env.setdefault("QSTACK_PYTHON", sys.executable)
    env.setdefault("SPINOR_REGISTRY_ROOT", str(package / "registry"))
    runtime_paths = [str(package / "bin")]
    repaired_libraries = package.parent / "heisenberg_photon.libs"
    if repaired_libraries.is_dir():
        runtime_paths.append(str(repaired_libraries))
    env["PATH"] = os.pathsep.join(runtime_paths) + os.pathsep + env.get("PATH", "")
    for compiler in ("spinorc", "phononc", "photonc"):
        binary = package / "bin" / (compiler + (".exe" if os.name == "nt" else ""))
        env.setdefault("QSTACK_" + compiler.upper(), str(binary))
    return subprocess.run([str(executable), *sys.argv[1:]], env=env, check=False).returncode


def spinorc() -> int:
    return _launch("spinorc")


def phononc() -> int:
    return _launch("phononc")


def photonc() -> int:
    return _launch("photonc")


def photonc_cxx() -> int:
    return _launch("photonc-cxx")
