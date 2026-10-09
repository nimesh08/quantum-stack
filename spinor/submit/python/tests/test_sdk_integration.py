"""Optional real SDK checks: serialization/verification only, never cloud jobs."""
import copy
from pathlib import Path

import pytest

from qstack.models import QStackError
from qstack.service import compile_file, find_binary


@pytest.fixture
def bell_source(tmp_path):
    try:
        find_binary("photonc")
    except QStackError:
        pytest.skip("C++ compilers not installed")
    path = tmp_path / "bell.pho"
    path.write_text("target generic\nkernel bell() -> int {\n QReg q(2)\n q.bell_pair(0, 1)\n return q.measure_int()\n}\n")
    return path


def test_real_qiskit_serializes_owned_native_circuit_without_transpilation(bell_source, monkeypatch):
    qiskit = pytest.importorskip("qiskit")
    pytest.importorskip("qiskit_ibm_runtime")
    from qiskit.quantum_info import Statevector
    from qstack.providers.native import qiskit_circuit
    monkeypatch.setattr(qiskit, "transpile", lambda *a, **k: pytest.fail("compiler called vendor transpiler"))
    artifact = compile_file(bell_source, target="ibm_fez")
    full = qiskit_circuit(artifact)
    assert full.num_qubits == artifact.physical_ir["num_qubits"]
    # Compact only the independent test oracle, never the artifact sent to IBM.
    compact = copy.deepcopy(artifact)
    compact.physical_ir["num_qubits"] = 1 + max(q for i in compact.physical_ir["instructions"] for q in i["qubits"])
    oracle = qiskit_circuit(compact).remove_final_measurements(inplace=False)
    probabilities = Statevector.from_instruction(oracle).probabilities_dict()
    assert probabilities.get("00") == pytest.approx(0.5)
    assert probabilities.get("11") == pytest.approx(0.5)


def test_real_pyqir_accepts_owned_bitcode(bell_source):
    pyqir = pytest.importorskip("pyqir")
    artifact = compile_file(bell_source, target="quantinuum_h2_1")
    assert artifact.format == "qir-bitcode"
    module = pyqir.Module.from_bitcode(pyqir.Context(), artifact.program_bytes())
    assert module.verify() is None
    assert "define i64 @main()" in str(module)
