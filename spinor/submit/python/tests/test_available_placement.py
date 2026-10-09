"""Discovered physical identities survive placement on active subgraphs."""
import pytest

from qstack.models import QStackError, SubmissionOptions
from qstack.registry import cache_targets, validate_physical
from qstack.service import compile_file, find_binary, submit_artifact


def snapshot(**fields):
    return {"route": "ibm", "vendor": "ibm", "device": "active-slot-fixture",
        "qubits": 3, "native_gates": ["x", "sx", "rz", "cz"],
        "all_to_all": False, "coupling": [[1, 2]], "formats": ["qiskit-native"],
        "capability_verified": True, "supports": {"feedforward": False, "reset": False,
        "mid_circuit_measure": False}, **fields}


@pytest.fixture
def compile_target(tmp_path, monkeypatch):
    try:
        find_binary("phononc")
    except QStackError:
        pytest.skip("C++ compilers not installed")
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path / "state"))
    def compile(record, level, bell=False, text=None):
        cache_targets(record["route"], [record])
        source = tmp_path / "source.phn"
        source.write_text(text or ("target generic\nqubit q[2]\nbit c[2]\nh q[0]\ncx q[0], q[1]\nc = measure q\n"
            if bell else "target generic\nqubit q[1]\nbit c[1]\nx q[0]\nc = measure q\n")
        )
        return compile_file(source, target=record["device"], config={"provider": record["route"]}, optimization_level=level)
    return compile


@pytest.mark.parametrize("level", range(4))
@pytest.mark.parametrize("record,expected,bell", [
    (snapshot(unavailable_qubits=[0]), {1, 2}, False),
    (snapshot(qubits=6, available_qubits=[5, 2], coupling=[[2, 5]]), {2, 5}, True),
    (snapshot(qubits=6, available_qubits=[5, 2], coupling=[[5, 2]], directed_connectivity=True,
              native_gates=["x", "sx", "rz", "cx"], gate_loci={"cx": [[5, 2]],
              "sx": [[2], [5]], "rz": [[2], [5]], "x": [[2], [5]], "measure": [[2], [5]]}), {2, 5}, True),
    (snapshot(gate_loci={"measure": [[2]]}), {2}, False),
    # An advertised but unused operation must not exclude the only X/readout slot.
    (snapshot(gate_loci={"x": [[1]], "measure": [[1]], "sx": [[0]]}), {1}, False),
])
def test_active_components_and_required_single_qubit_loci(compile_target, level, record, expected, bell):
    artifact = compile_target(record, level, bell)
    ir = artifact.physical_ir
    assert ir["num_qubits"] == record["qubits"]
    assert set(ir["logical_to_physical"]) <= expected
    assert set(ir["initial_logical_to_physical"]) <= expected
    assert {q for inst in ir["instructions"] for q in inst.get("qubits", [])} <= expected
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=64), wait=True)
    assert set(result.counts) == ({"00", "11"} if bell else {"1"})
    assert sum(result.counts.values()) == 64


@pytest.mark.parametrize("level", range(4))
def test_explicit_empty_readout_loci_and_disconnected_active_components_reject(compile_target, level):
    with pytest.raises(QStackError, match="usable physical subgraph"):
        compile_target(snapshot(gate_loci={"measure": []}), level)
    with pytest.raises(QStackError, match="connected component"):
        compile_target(snapshot(available_qubits=[0, 2], coupling=[[0, 1], [1, 2]]), level, bell=True)


@pytest.mark.parametrize("level", range(4))
def test_disabled_resonator_never_appears_in_move_path(compile_target, level):
    record = snapshot(route="iqm", vendor="iqm", qubits=5, native_gates=["u1q", "cz", "move"],
        computational_qubits=[0, 2, 3], resonator_qubits=[1, 4], unavailable_qubits=[4],
        qubit_labels=["QB1", "CR1", "QB2", "QB3", "CR2"], formats=["iqm-json"],
        gate_loci={"u1q": [[0], [2], [3]], "measure": [[0], [2], [3]],
            "move": [[0, 1], [2, 1], [2, 4], [3, 4]], "cz": [[0, 1], [2, 1], [2, 4], [3, 4]]},
        coupling=[[0, 1], [2, 1], [2, 4], [3, 4]], directed_connectivity=True)
    artifact = compile_target(record, level, bell=True)
    assert artifact.physical_ir["resonator_qubits"] == [1, 4]
    used = {q for inst in artifact.physical_ir["instructions"] for q in inst.get("qubits", [])}
    assert used <= {0, 1, 2} and 1 in used
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=64), wait=True)
    assert set(result.counts) == {"00", "11"}


def test_revalidation_rejects_physical_slot_outside_available_components():
    ir = {"num_qubits": 3, "num_clbits": 0, "instructions": [{"op": "x", "qubits": [0]}]}
    with pytest.raises(QStackError, match="available component"):
        validate_physical(ir, snapshot(available_qubits=[1, 2]))
    ir["instructions"] = []
    ir["logical_to_physical"] = [0]
    with pytest.raises(QStackError, match="layout includes an unavailable"):
        validate_physical(ir, snapshot(available_qubits=[1, 2]))


@pytest.mark.parametrize("level", range(4))
def test_optimization_keeps_legal_baseline_when_a_replacement_gate_is_unavailable(compile_target, level):
    record = snapshot(qubits=1, coupling=[], gate_loci={"sx": [[0]], "x": [], "measure": [[0]]})
    artifact = compile_target(record, level, text="target generic\nqubit q[1]\nbit c[1]\nsx q[0]\nsx q[0]\nc = measure q\n")
    assert all(inst["op"] != "x" for inst in artifact.physical_ir["instructions"])
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=16), wait=True)
    assert result.counts == {"1": 16}
