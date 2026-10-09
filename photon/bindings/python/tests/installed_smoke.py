"""Installed-wheel smoke test: engine, all launchers, registry, local Bell run."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

for name in ("QSTACK_SPINORC", "QSTACK_PHONONC", "QSTACK_PHOTONC", "QSTACK_PYTHONPATH", "SPINOR_REGISTRY_ROOT"):
    os.environ.pop(name, None)

import photon
import qstack
from qstack.service import run_source

assert photon.__version__ == "0.6.0"
assert photon._ENGINE_AVAILABLE, getattr(photon, "_IMPORT_ERROR", "engine missing")
package = Path(photon.__file__).parent
assert len(list((package / "registry/chips").glob("*.yaml"))) == 31
for name in ("spinorc", "phononc", "photonc", "photonc-cxx"):
    launcher = shutil.which(name)
    assert launcher, f"{name} console launcher is missing"
    binary = package / "bin" / (name + (".exe" if os.name == "nt" else ""))
    assert binary.is_file(), binary
    subprocess.run([launcher, "--help"], check=True, capture_output=True, text=True)
# Engine parsing/lowering and the actual C++ simulation both run offline.
source = "target generic\nqubit q[2]\nbit c[2]\nh q[0]\ncx q[0], q[1]\nc = measure q\n"
compiled = photon.compile_phonon(source, "generic")
assert compiled.ok, compiled.error
with tempfile.TemporaryDirectory(prefix="photon-wheel-smoke-") as temp:
    previous = Path.cwd()
    try:
        os.chdir(temp)
        result = run_source(source, language="phonon", target="ibm_heron_r2", mode="local", shots=128)
    finally:
        os.chdir(previous)
assert result.counts and sum(result.counts.values()) == 128, result.counts
assert set(result.counts) <= {"00", "11"}, result.counts
print("Installed Photon wheel: extension, four CLI binaries, registry, and real local Bell execution passed")
