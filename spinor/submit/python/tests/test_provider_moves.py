"""IQM resonator safety contracts, independent of the optional SDK."""
import json

import pytest

from qstack.models import QStackError
from qstack.providers.native import serialize_native
from qstack.registry import validate_physical


def star():
    return {"qubits": 4, "qubit_labels": ["QB1", "QB2", "CR1", "CR2"],
        "computational_qubits": [0, 1], "resonator_qubits": [2, 3],
        "native_gates": ["u1q", "cz", "move", "measure"], "all_to_all": False,
        "coupling": [[0, 2], [1, 2], [0, 3], [1, 3]], "directed_connectivity": False,
        "gate_loci": {"move": [[0, 2], [1, 2], [0, 3]], "cz": [[1, 2], [1, 3]]},
        "supports": {"reset": False, "mid_circuit_measure": False, "feedforward": False}}


def circuit(operations):
    return {"num_qubits": 4, "num_clbits": 2, "instructions": operations,
        "measurement_mapping": [], "logical_to_physical": [0, 1], "initial_logical_to_physical": [0, 1],
        "computational_qubits": [0, 1], "resonator_qubits": [2, 3]}


def move(q=0, r=2):
    return {"op": "move", "qubits": [q, r]}


def test_owned_iqm_move_sandwich_keeps_physical_component_names():
    ir = circuit([move(), {"op": "cz", "qubits": [1, 2]}, move()])
    validate_physical(ir, star())
    fmt, payload = serialize_native("iqm", ir, star())
    assert fmt == "iqm-json"
    assert '"name": "move", "locus": ["QB1", "CR1"]' in payload
    assert '"name": "cz", "locus": ["QB2", "CR1"]' in payload


@pytest.mark.parametrize("operations", [
    [move()],
    [move(), move(1)],
    [move(), move(0, 3)],
    [move(), {"op": "u1q", "qubits": [0], "params": [0.2, 0.3]}, move()],
    [{"op": "cz", "qubits": [1, 2]}],
    [{"op": "u1q", "qubits": [2], "params": [0.2, 0.3]}],
    [move(2, 0), move(2, 0)],
])
def test_invalid_iqm_resonator_sequences_fail_before_serialization(operations):
    for check in (lambda: validate_physical(circuit(operations), star()),
                  lambda: serialize_native("iqm", circuit(operations), star())):
        with pytest.raises(QStackError):
            check()


def test_resonators_cannot_hold_logical_layout_slots():
    ir = circuit([move(), move()])
    ir["initial_logical_to_physical"] = [0, 2]
    with pytest.raises(QStackError, match="reserved"):
        validate_physical(ir, star())
    ir = circuit([move(), move()])
    ir["resonator_qubits"] = [3]
    with pytest.raises(QStackError, match="metadata"):
        serialize_native("iqm", ir, star())


def test_iqm_repeated_classical_assignment_has_unique_server_keys():
    ir = circuit([{"op": "measure", "qubits": [0], "clbits": [0]},
                  {"op": "measure", "qubits": [1], "clbits": [0]}])
    _, payload = serialize_native("iqm", ir, star())
    assert [inst["args"]["key"] for inst in json.loads(payload)["instructions"]] == ["c0", "c0__1"]


def test_component_partition_order_does_not_change_physical_slots():
    snapshot = {**star(), "qubits": 3, "qubit_labels": ["QB1", "CR1", "QB2"],
        "computational_qubits": [2, 0], "resonator_qubits": [1],
        "coupling": [[0, 1], [2, 1]],
        "gate_loci": {"move": [[0, 1], [2, 1]], "cz": [[2, 1]]}}
    ir = {**circuit([move(0, 1), {"op": "cz", "qubits": [2, 1]}, move(0, 1)]),
        "num_qubits": 3, "computational_qubits": [0, 2], "resonator_qubits": [1],
        "logical_to_physical": [2, 0], "initial_logical_to_physical": [2, 0]}
    validate_physical(ir, snapshot)
    _, payload = serialize_native("iqm", ir, snapshot)
    assert json.loads(payload)["instructions"][0]["locus"] == ["QB1", "CR1"]


@pytest.mark.parametrize("location", ["ir", "snapshot"])
@pytest.mark.parametrize("field,value", [
    ("computational_qubits", [0, 1, 1]),
    ("computational_qubits", [0]),
    ("resonator_qubits", [2, 3, 3]),
    ("resonator_qubits", [2]),
    ("computational_qubits", [False, 1]),
])
def test_component_partition_duplicates_and_missing_slots_are_rejected(location, field, value):
    ir, snapshot = circuit([move(), move()]), star()
    (ir if location == "ir" else snapshot)[field] = value
    for check in (lambda: validate_physical(ir, snapshot),
                  lambda: serialize_native("iqm", ir, snapshot)):
        with pytest.raises(QStackError):
            check()
