"""Native HQSLIB serialization and its explicit semantic boundaries."""
import copy
import json
import math

import pytest

from qstack.models import QStackError
from qstack.providers.qasm2 import serialize_qasm2


def program(*operations):
    return {"schema_version": 1, "num_qubits": 2, "num_clbits": 6, "global_phase": 0.25,
            "instructions": list(operations)}


def test_hqslib_native_names_radians_physical_order_and_sparse_measurements():
    ir = program({"op": "u1q", "qubits": [1], "params": [0.5, 0.125]},
        {"op": "rz", "qubits": [0], "params": [-0.25]},
        {"op": "rzz", "qubits": [1, 0], "params": [0.75]},
        {"op": "measure", "qubits": [1], "clbits": [5]},
        {"op": "measure", "qubits": [0], "clbits": [0]})
    original = copy.deepcopy(ir)
    text = serialize_qasm2(ir, {"vendor": "quantinuum", "route": "azure"})
    assert 'include "hqslib1.inc";' in text and "creg c[6];" in text
    assert "U1q(0.5,0.125) q[1];" in text
    assert "Rz(-0.25) q[0];" in text and "RZZ(0.75) q[1],q[0];" in text
    assert "measure q[1] -> c[5];" in text and "measure q[0] -> c[0];" in text
    assert "Scalar phase retained in physical IR: 0.25 radians" in text
    assert ir == original


def test_hqslib_simple_if_else_preserves_bit_predicate_and_branch_phase():
    ir = program({"op": "measure", "qubits": [0], "clbits": [5]},
        {"op": "if", "clbits": [5], "condition_value": 0},
        {"op": "gphase", "params": [0.125]},
        {"op": "rz", "qubits": [1], "params": [0.5]}, {"op": "else"},
        {"op": "reset", "qubits": [1]}, {"op": "endif"})
    text = serialize_qasm2(ir)
    assert "if(c[5]==0) Rz(0.5) q[1];" in text
    assert "if(c[5]==1) reset q[1];" in text
    assert "// if(c[5]==0) Scalar phase retained in physical IR: 0.125" in text
    assert not any("gphase" in line for line in text.splitlines() if not line.startswith("//"))


@pytest.mark.parametrize("body,match", [
    ([{"op": "if", "clbits": [0], "condition_value": 1}], "Nested"),
    ([{"op": "measure", "qubits": [0], "clbits": [5]}], "condition bit"),
    ([{"op": "barrier", "qubits": [0, 1]}], "Conditional barriers"),
])
def test_hqslib_rejects_branches_that_cannot_preserve_ir_semantics(body, match):
    ir = program({"op": "if", "clbits": [5], "condition_value": 0}, *body, {"op": "endif"})
    with pytest.raises(QStackError, match=match):
        serialize_qasm2(ir)


@pytest.mark.parametrize("operations", [
    [{"op": "else"}], [{"op": "endif"}], [{"op": "if", "clbits": [0], "condition_value": 1}],
    [{"op": "if", "clbits": [0], "condition_value": 1}, {"op": "else"}, {"op": "else"}],
    [{"op": "measure", "qubits": [1], "clbits": [6]}],
    [{"op": "rz", "qubits": [0], "params": [float("nan")]}],
    [{"op": "rzz", "qubits": [0, 0], "params": [0.5]}],
    [{"op": "u1q", "qubits": [0], "params": [0.5]}],
])
def test_hqslib_rejects_invalid_or_incomplete_ir(operations):
    with pytest.raises(QStackError):
        serialize_qasm2(program(*operations))


def test_hqslib_does_not_rebase_abstract_gates_or_non_quantinuum_targets():
    with pytest.raises(QStackError, match="not a native"):
        serialize_qasm2(program({"op": "h", "qubits": [0]}))
    with pytest.raises(QStackError, match="Quantinuum target"):
        serialize_qasm2(program(), {"vendor": "ibm"})


def test_real_quantinuum_parser_and_matrix_oracle_validate_native_qasm2():
    pytest.importorskip("pytket")
    import numpy as np
    from pytket.qasm import circuit_from_qasm_str
    # HQSLIB extends QASM2 with capital native identifiers and bit conditions.
    # Its actual provider parser is an oracle only, never a runtime compiler.
    ir = program({"op": "u1q", "qubits": [1], "params": [0.71, -0.24]},
        {"op": "rz", "qubits": [0], "params": [-0.6]}, {"op": "rzz", "qubits": [1, 0], "params": [0.83]})
    native = circuit_from_qasm_str(serialize_qasm2(ir))
    equatorial = np.array([[0, np.exp(0.24j)], [np.exp(-0.24j), 0]])
    u1q = math.cos(0.71 / 2) * np.eye(2) - 1j * math.sin(0.71 / 2) * equatorial
    rz = np.diag(np.exp(-0.5j * -0.6 * np.array([1, -1])))
    rzz = np.diag(np.exp(-0.5j * 0.83 * np.array([1, -1, -1, 1])))
    expected = rzz @ np.kron(rz, np.eye(2)) @ np.kron(np.eye(2), u1q)
    assert np.allclose(native.get_unitary(), expected, atol=1e-12)
    dynamic = program({"op": "measure", "qubits": [0], "clbits": [5]},
        {"op": "if", "clbits": [5], "condition_value": 0},
        {"op": "rz", "qubits": [1], "params": [0.5]}, {"op": "else"},
        {"op": "reset", "qubits": [1]}, {"op": "endif"})
    commands = circuit_from_qasm_str(serialize_qasm2(dynamic)).get_commands()
    assert len(commands) == 3
    assert [command.op.value for command in commands[1:]] == [0, 1]
    assert [str(command.args[0]) for command in commands[1:]] == ["c[5]", "c[5]"]


def test_parameter_free_compiled_ms_serializes_as_fully_entangling_native_gate(tmp_path):
    from qstack.service import compile_file, find_binary
    try:
        find_binary("photonc")
    except QStackError:
        pytest.skip("C++ compilers not installed")
    source = tmp_path / "bell.pho"
    source.write_text("target generic\nkernel bell() -> int {\n QReg q(2)\n q.bell_pair(0, 1)\n return q.measure_int()\n}\n")
    artifact = compile_file(source, target="ionq_aria_1", config={"provider": "ionq"})
    ms = [op for op in json.loads(artifact.program_text())["circuit"] if op["gate"] == "ms"]
    assert ms and all(op["phases"] == [0.0, 0.0] and op["angle"] == 0.25 for op in ms)
