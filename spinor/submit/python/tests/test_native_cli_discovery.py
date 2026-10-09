"""The preserved native CLIs also work directly from a source-tree build."""
import os
from pathlib import Path
import subprocess

import pytest

from qstack.models import QStackError
from qstack.service import find_binary


@pytest.mark.parametrize("driver,source", [
    ("phononc", "phonon/tests/corpus/bell_pair_func.phn"),
    ("photonc", "examples/qstack/bell.pho"),
])
def test_native_driver_finds_source_tree_spinorc_without_override(tmp_path, driver, source):
    root = Path(__file__).resolve().parents[4]
    try:
        executable = find_binary(driver)
        spinorc = find_binary("spinorc")
    except QStackError:
        pytest.skip("Native compiler build required")
    if not Path(spinorc).resolve().is_relative_to(root / "build"):
        pytest.skip("This test exercises source-tree discovery, not an installed binary")
    env = {key: value for key, value in os.environ.items() if key != "QSTACK_SPINORC"}
    # Keep runtime DLL directories, removing only directories containing a
    # compiler executable so an inherited PATH cannot hide a broken fallback.
    env["PATH"] = os.pathsep.join(entry for entry in env.get("PATH", "").split(os.pathsep)
        if entry and not any((Path(entry) / name).is_file() for name in ("spinorc", "spinorc.exe")))
    output = tmp_path / "native program.spn"
    completed = subprocess.run([str(executable), "compile", "--target", "ibm_fez", "--emit", "spinor",
                                str(root / source), "--out", str(output)], cwd=root, env=env,
                               text=True, capture_output=True, timeout=60)
    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert output.is_file() and "measure" in output.read_text(encoding="utf-8")
