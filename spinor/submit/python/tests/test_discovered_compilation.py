"""All cloud output routes through real owned compilation, with offline fixtures."""
import pytest

from qstack.models import QStackError, SubmissionOptions
from qstack.registry import cache_targets
from qstack.service import compile_file, find_binary, submit_artifact


@pytest.mark.parametrize("route,vendor,gates,fmt", [
    ("ibm", "ibm", ["rz", "sx", "x", "cz"], "qiskit-native"),
    ("google", "google", ["phased_xz", "sqrt_iswap"], "cirq-native"),
    ("quantinuum", "quantinuum", ["u1q", "rz", "rzz"], "qir-bitcode"),
    ("azure", "quantinuum", ["u1q", "rz", "rzz"], "qir-bitcode"),
    ("azure", "ionq", ["gpi", "gpi2", "ms"], "ionq-native-json"),
    ("aws", "ionq", ["gpi", "gpi2", "ms"], "openqasm3"),
    ("aws", "iqm", ["u1q", "cz"], "openqasm3"),
    ("ionq", "ionq", ["gpi", "gpi2", "ms"], "ionq-native-json"),
    ("rigetti", "rigetti", ["rx", "rz", "iswap"], "quil"),
    ("iqm", "iqm", ["u1q", "cz"], "iqm-json"),
    ("oqc", "oqc", ["rz", "sx", "x", "ecr"], "openqasm3"),
    ("aqt", "aqt", ["u1q", "rz", "rxx"], "aqt-native"),
    ("anyon", "anyon", ["rz", "sx", "x", "cz"], "anyon-json"),
    ("alicebob", "alicebob", ["x", "z"], "qir-text"),
])
def test_discovered_snapshot_to_native_artifact_to_real_simulation(route, vendor, gates, fmt, tmp_path, monkeypatch):
    try:
        find_binary("photonc")
    except QStackError:
        pytest.skip("C++ compilers not installed")
    if fmt == "qir-bitcode":
        pytest.importorskip("pyqir")
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path / "state"))
    single = route == "alicebob"
    snapshot = {"route": route, "vendor": vendor, "device": "offline-contract-fixture", "qubits": 1 if single else 2,
        "qir_platform": "quantinuum-h2" if vendor == "quantinuum" else "standard",
        "native_gates": gates, "all_to_all": True, "coupling": [], "formats": [fmt], "capability_verified": True,
        "capability_sources": ["offline test fixture, no hardware validation"], "parameter_units": "radians",
        "supports": {"reset": False, "feedforward": False, "mid_circuit_measure": False},
        "qubit_labels": ["0_0", "0_1"] if route == "google" else ["QB1", "QB2"]}
    cache_targets(route, [snapshot])
    source = tmp_path / "source.pho"
    source.write_text("target generic\nkernel sample() -> int {\n QReg q(" + ("1" if single else "2") +
                      ")\n q." + ("x(0)" if single else "bell_pair(0, 1)") + "\n return q.measure_int()\n}\n")
    artifact = compile_file(source, target=snapshot["device"], config={"provider": route}, output=tmp_path / "bundle")
    from qstack.verification import verify_artifact
    evidence = verify_artifact(artifact)
    assert evidence["status"] == "passed", evidence
    assert evidence["artifact_hash"] == artifact.manifest["artifact_hash"]
    assert evidence["network_used"] is False
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=64), wait=True)
    assert set(result.counts) == ({"1"} if single else {"00", "11"})
    assert sum(result.counts.values()) == 64
    assert result.metadata["measurement_mapping"] == artifact.physical_ir["measurement_mapping"]
    assert result.metadata["artifact_hash"] == artifact.manifest["artifact_hash"]
    from qstack.jobs import load_job
    saved_receipt, saved_result = load_job(result.job_id)
    assert saved_receipt.metadata["measurement_mapping"] == result.metadata["measurement_mapping"]
    assert saved_result.to_dict() == result.to_dict()
    if route == "aws":
        assert "#pragma braket verbatim" in artifact.program_text()
        assert artifact.program_text().rfind("measure") > artifact.program_text().rfind("}")
        if vendor == "iqm":
            assert "prx(" in artifact.program_text()


def test_discovered_calibration_reaches_owned_placement_and_timing(tmp_path, monkeypatch):
    try:
        find_binary("photonc")
    except QStackError:
        pytest.skip("C++ compilers not installed")
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path / "state"))
    snapshot = {"device": "calibrated-fixture", "vendor": "ibm", "qubits": 3, "native_gates": ["x", "rz", "sx", "cz"],
        "all_to_all": True, "coupling": [], "formats": ["qiskit-native"], "capability_verified": True,
        "readout_errors": [[0, 0.3], [1, 0.2], [2, 0.01]],
        "one_qubit_errors": [[0, 0.03], [1, 0.02], [2, 0.001]],
        "instruction_durations": [{"op": op, "qubits": [q], "duration_ns": 20 if op == "x" else 100}
                                  for op in ("x", "measure") for q in range(3)]}
    cache_targets("ibm", [snapshot])
    source = tmp_path / "source.pho"
    source.write_text("target generic\nkernel sample() -> int { QReg q(1)\nq.x(0)\nreturn q.measure_int()\n}\n")
    artifact = compile_file(source, target=snapshot["device"], config={"provider": "ibm"}, optimization_level=3)
    assert artifact.physical_ir["logical_to_physical"] == [2]
    assert artifact.manifest["statistics"]["duration_seconds"] == pytest.approx(120e-9)


@pytest.mark.parametrize("optimization_level", range(4))
@pytest.mark.parametrize("width", [2, 3])
def test_discovered_iqm_resonators_compile_and_run_bell_ghz(width, optimization_level, tmp_path, monkeypatch):
    import json
    from qstack.artifacts import load_artifact
    try:
        find_binary("photonc")
    except QStackError:
        pytest.skip("C++ compilers not installed")
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path / "state"))
    snapshot = {"route": "iqm", "vendor": "iqm", "device": "two-resonator-fixture", "qubits": 5,
        "computational_qubits": [0, 2, 3], "resonator_qubits": [1, 4],
        "qubit_labels": ["QB1", "CR1", "QB2", "QB3", "CR2"],
        "native_gates": ["u1q", "cz", "move"], "formats": ["iqm-json"],
        "gate_loci": {"u1q": [[0], [2], [3]], "measure": [[0], [2], [3]],
            "move": [[0, 1], [2, 1], [2, 4], [3, 4]], "cz": [[0, 1], [2, 1], [2, 4], [3, 4]]},
        "coupling": [[0, 1], [2, 1], [2, 4], [3, 4]], "all_to_all": False, "directed_connectivity": True,
        "parameter_units": "radians", "capability_verified": True,
        "supports": {"reset": False, "feedforward": False, "mid_circuit_measure": False},
        "capability_sources": ["offline SDK-contract fixture, no hardware validation"]}
    cache_targets("iqm", [snapshot])
    source = tmp_path / "ghz.pho"
    source.write_text("target generic\nkernel sample() -> int {\n QReg q(" + str(width) + ")\nq.h(0)\n" +
        "\n".join(f"q.cx({q-1}, {q})" for q in range(1, width)) + "\nreturn q.measure_int()\n}\n")
    artifact = compile_file(source, target=snapshot["device"], config={"provider": "iqm"},
                            optimization_level=optimization_level, output=tmp_path / "artifact")
    loaded = load_artifact(tmp_path / "artifact")
    ir = loaded.physical_ir
    assert ir["resonator_qubits"] == [1, 4]
    assert ir["computational_qubits"] == [0, 2, 3]
    assert set(ir["logical_to_physical"]) <= {0, 2, 3}
    assert any(op["op"] == "move" for op in ir["instructions"])
    native = json.loads(artifact.program_text())
    assert any(op["name"] == "move" and op["locus"][1].startswith("CR") for op in native["instructions"])
    result = submit_artifact(loaded, SubmissionOptions(mode="local", shots=128), wait=True)
    assert set(result.counts) == {"0" * width, "1" * width}
    assert sum(result.counts.values()) == 128
    assert result.metadata["measurement_mapping"] == ir["measurement_mapping"]
