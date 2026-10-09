import copy
import json
import pytest

from qstack.models import CompiledArtifact
from qstack.verification import verify_artifact


def test_reserved_ancillas_are_initialized_and_traced_against_entangled_references():
    from copy import deepcopy
    from qstack.verification.engine import compare
    ir = {"schema_version": 2, "num_qubits": 2, "num_clbits": 0, "global_phase": 0,
          "quantum_inputs": [0], "reserved_pool": [1],
          "instructions": [{"op": "cx", "qubits": [0, 1]}]}
    options = dict(max_qubits=2, max_paths=16, threshold=2e-13)
    # Entangling with a discarded ancilla dephases an arbitrary retained input.
    dropped = deepcopy(ir)
    dropped["instructions"] = []
    assert compare(ir, dropped, **options)["status"] == "failed"
    equivalent = deepcopy(ir)
    equivalent["instructions"].append({"op": "z", "qubits": [1]})
    assert compare(ir, equivalent, **options)["status"] == "passed"
    # Resetting the entangled ancilla has the same reduced channel; forgetting
    # the entanglement before reuse does not.
    equivalent["instructions"] = [*ir["instructions"], {"op": "reset", "qubits": [1]}]
    assert compare(ir, equivalent, **options)["status"] == "passed"
    bad_initialization = deepcopy(ir)
    bad_initialization["instructions"] = [{"op": "x", "qubits": [1]}, {"op": "cx", "qubits": [1, 0]}]
    assert compare(dropped, bad_initialization, **options)["status"] == "failed"


def test_oracle_rejects_out_of_range_shifts_before_evaluation():
    from qstack.verification.engine import _classical
    from qstack.verification.parsers import classical_expression
    for amount in (8, 2**64-1):
        with pytest.raises(ValueError, match="range"):
            _classical({"op": "c_shl", "result": "r", "inputs": ["a", "b"]},
                       {"a": 1, "b": amount}, {"r": {"width": 8}})
        with pytest.raises(ValueError, match="range"):
            classical_expression("a << b", {"a": 1, "b": amount}, [], {"a": 8, "b": 64})


def test_complete_instrument_budget_counts_all_branch_paths():
    from qstack.verification import NotChecked
    from qstack.verification.engine import execute
    ir = {"num_qubits": 2, "num_clbits": 2, "instructions": [
        {"op": "measure", "qubits": [0], "clbits": [0]},
        {"op": "if", "clbits": [0], "condition_value": 1},
        {"op": "measure", "qubits": [1], "clbits": [1]},
        {"op": "else"}, {"op": "measure", "qubits": [1], "clbits": [1]}, {"op": "endif"}]}
    with pytest.raises(NotChecked, match="budget.*branch join"):
        execute(ir, max_qubits=2, max_paths=3)
    assert len(execute(ir, max_qubits=2, max_paths=4)[1]) == 4


def test_physical_storage_oracle_detects_overlapping_live_values():
    from qstack.verification.engine import compare
    ir = {"schema_version": 2, "num_qubits": 1, "num_clbits": 3, "exported_clbits": [2],
          "classical_values": [{"id": name, "type": "bool", "width": 1, "storage": [bit]}
                               for name, bit in (("a", 0), ("b", 1), ("out", 2))],
          "classical_outputs": [{"name": "out", "type": "bool", "width": 1, "value": "out"}],
          "instructions": [{"op": "c_const", "result": "a", "value": "1"},
                           {"op": "c_const", "result": "b", "value": "0"},
                           {"op": "c_copy", "result": "out", "inputs": ["a"]}]}
    broken = copy.deepcopy(ir)
    broken["classical_values"][1]["storage"] = [0]
    result = compare(ir, broken, max_qubits=1, max_paths=4, threshold=2e-13)
    assert result["status"] == "failed"


def test_dense_instrument_memory_limit_is_reported_before_allocation():
    from qstack.verification import NotChecked
    from qstack.verification.engine import execute
    with pytest.raises(NotChecked, match="working-memory budget"):
        execute({"num_qubits": 6, "num_clbits": 0, "instructions": []},
                max_qubits=6, max_paths=256)


def program(fmt="qasm3", measured=False):
    ops = [{"op": "ry", "qubits": [0], "params": [.37]}, {"op": "cx", "qubits": [0, 1]}]
    if measured: ops.append({"op": "measure", "qubits": [1], "clbits": [2]})
    ir = {"schema_version": 2, "num_qubits": 2, "num_clbits": 3, "instructions": ops}
    payload = "OPENQASM 3.0; include \"stdgates.inc\"; qubit[2] q; bit[3] c; ry(0.37) q[0]; cx q[0],q[1];"
    if measured: payload += "c[2] = measure q[1];"
    return CompiledArtifact("ibm", "offline", fmt, payload, ir, schema_version=2, logical_ir=copy.deepcopy(ir))


def test_offline_full_operator_and_correlated_instrument(monkeypatch):
    import socket
    monkeypatch.setattr(socket, "socket", lambda *a, **k: pytest.fail("Offline verifier opened network"))
    for measured in (False, True):
        result = verify_artifact(program(measured=measured), store=False)
        assert result["status"] == "passed", result
        assert result["network_used"] is False and result["certified"] is False


@pytest.mark.parametrize("mutation", ["angle", "operands", "readout"])
def test_independent_parser_detects_serializer_mutations(mutation):
    artifact = program(measured=True)
    if mutation == "angle": artifact.payload = artifact.payload.replace("0.37", "0.73")
    if mutation == "operands": artifact.payload = artifact.payload.replace("cx q[0],q[1]", "cx q[1],q[0]")
    if mutation == "readout": artifact.payload = artifact.payload.replace("c[2] = measure q[1];", "")
    evidence = verify_artifact(artifact, store=False)
    assert evidence["status"] == "failed", evidence


def test_unknown_instruction_not_silently_ignored():
    artifact = program()
    artifact.payload += "alien q[0];"
    result = verify_artifact(artifact, store=False)
    assert result["status"] == "not_checked"
    assert "not skipped" in result["checks"][1]["reason"]


def test_phase_is_checked_for_qasm3():
    artifact = program()
    artifact.payload += "gphase(0.123);"
    assert verify_artifact(artifact, store=False)["status"] == "failed"


def test_custom_gate_body_interpreted_not_assumed():
    artifact = program()
    artifact.payload = artifact.payload.replace("cx q[0],q[1];", "mystery q[0],q[1];")
    artifact.payload = "gate mystery a,b { cx b,a; }\n" + artifact.payload
    assert verify_artifact(artifact, store=False)["status"] == "failed"


def test_reset_and_overwritten_branch_predicate_complete_instrument():
    artifact = program(measured=True)
    extra = [{"op": "if", "clbits": [2], "condition_value": 1}, {"op": "reset", "qubits": [0]},
             {"op": "measure", "qubits": [0], "clbits": [2]}, {"op": "x", "qubits": [1]},
             {"op": "else"}, {"op": "z", "qubits": [1]}, {"op": "endif"}]
    artifact.physical_ir["instructions"].extend(extra)
    artifact.logical_ir = copy.deepcopy(artifact.physical_ir)
    artifact.payload += "if (c[2] == 1) { reset q[0]; c[2] = measure q[0]; x q[1]; } else { z q[1]; }"
    result = verify_artifact(artifact, store=False)
    assert result["status"] == "passed", result


def test_bounds_and_v1_missing_evidence_are_explicit():
    artifact = program()
    result = verify_artifact(artifact, max_qubits=1, store=False)
    assert result["status"] == "not_checked"
    old = CompiledArtifact("ibm", "old", "qasm3", artifact.payload, {**artifact.physical_ir, "schema_version": 1})
    assert verify_artifact(old, store=False)["checks"][0]["status"] == "not_checked"


def test_tiny_interaction_mutation_strict_threshold():
    artifact = program()
    artifact.physical_ir["instructions"] = [{"op": "rxx", "qubits": [0, 1], "params": [2e-12]}]
    artifact.logical_ir = copy.deepcopy(artifact.physical_ir)
    artifact.payload = "OPENQASM 3.0; qubit[2] q; bit[3] c;"
    assert verify_artifact(artifact, threshold=2e-13, store=False)["status"] == "failed"


def test_independent_qir_measurement_and_output_permutation():
    pytest.importorskip("pyqir")
    artifact = program(measured=True)
    artifact.format = "qir-text"
    artifact.payload = '''
declare void @__quantum__qis__ry__body(double, ptr)
declare void @__quantum__qis__cnot__body(ptr, ptr)
declare void @__quantum__qis__mz__body(ptr, ptr)
declare void @__quantum__rt__array_record_output(i64, ptr)
declare void @__quantum__rt__bool_record_output(i1, ptr)
declare void @__quantum__rt__result_record_output(ptr, ptr)
define i64 @main() {
entry:
 call void @__quantum__qis__ry__body(double 3.700000e-1, ptr null)
 call void @__quantum__qis__cnot__body(ptr null, ptr inttoptr (i64 1 to ptr))
 call void @__quantum__qis__mz__body(ptr inttoptr (i64 1 to ptr), ptr null)
 call void @__quantum__rt__array_record_output(i64 3, ptr null)
 call void @__quantum__rt__bool_record_output(i1 false, ptr null)
 call void @__quantum__rt__bool_record_output(i1 false, ptr null)
 call void @__quantum__rt__result_record_output(ptr null, ptr null)
 ret i64 0
}
'''
    result = verify_artifact(artifact, store=False)
    assert result["status"] == "passed", result
    artifact.payload = artifact.payload.replace(' call void @__quantum__rt__result_record_output(ptr null, ptr null)', '')
    assert verify_artifact(artifact, store=False)["status"] == "failed"


def test_quil_explicit_matrix_and_braket_verbatim():
    artifact = program(measured=True)
    artifact.format = "quil"
    artifact.payload = "DECLARE ro BIT[3]\nRY(0.37) 0\nCNOT 0 1\nMEASURE 1 ro[2]\n"
    result = verify_artifact(artifact, store=False)
    assert result["status"] == "passed", result
    artifact.format = "qasm3"
    artifact.payload = "OPENQASM 3.0; bit[3] c;\n#pragma braket verbatim\nbox { ry(0.37) $0; cx $0,$1; } c[2] = measure $1;"
    assert verify_artifact(artifact, store=False)["status"] == "passed"
