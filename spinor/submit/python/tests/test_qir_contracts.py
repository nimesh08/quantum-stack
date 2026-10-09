"""QIR profile contracts and classical control semantics, without cloud execution.

The classical oracle runs LLVM branch/phi instructions with supplied measurement
outcomes. LLVM verification alone cannot detect incorrect branch assignment joins.
"""
from __future__ import annotations

import re

import pytest

from qstack.models import CompiledArtifact, QStackError, SubmissionOptions
from qstack.providers import get_adapter
from qstack.service import compile_file, find_binary


@pytest.fixture
def compile_qir(tmp_path):
    pyqir = pytest.importorskip("pyqir")
    try:
        find_binary("spinorc")
    except QStackError:
        pytest.skip("C++ compiler not installed")

    def compile_source(source, target="quantinuum_h2_1"):
        path = tmp_path / "contract.spn"
        path.write_text("target generic\n" + source, encoding="utf-8")
        artifact = compile_file(path, target=target, optimization_level=0, format="qir-text")
        module = pyqir.Module.from_ir(pyqir.Context(), artifact.program_text())
        assert module.verify() is None
        return artifact, module

    return compile_source


def run_classical_control(module, measurements):
    """Interpret the generated classical subset, stubbing only physical readout."""
    import pyqir

    entry = next(f for f in module.functions if f.name == "main")
    block, previous, values, output, measured = entry.basic_blocks[0], None, {}, [], set()

    def value(v):
        return v.value if isinstance(v, pyqir.IntConstant) else values[v.name]

    for _ in range(100):
        for instruction in block.instructions:
            if isinstance(instruction, pyqir.Phi):
                incoming = next(v for v, predecessor in instruction.incoming if predecessor.name == previous)
                values[instruction.name] = value(incoming)
            elif isinstance(instruction, pyqir.Call):
                name = instruction.callee.name
                if name == "__quantum__qis__mz__body":
                    measured.add(pyqir.ptr_id(instruction.args[1]))
                elif name in {"__quantum__rt__read_result", "__quantum__qis__read_result__body"}:
                    result = pyqir.ptr_id(instruction.args[0])
                    assert result in measured, "Read from a measurement result absent on this control path"
                    values[instruction.name] = measurements[result]
                elif name == "__quantum__rt__bool_record_output":
                    output.append(value(instruction.args[0]))
                elif name == "__quantum__rt__result_record_output":
                    result = pyqir.ptr_id(instruction.args[0])
                    assert result in measured
                    output.append(measurements[result])
            elif instruction.opcode == pyqir.Opcode.BR:
                previous = block.name
                # PyQIR follows LLVM operand order: condition, false block, true block.
                operands = instruction.operands
                block = operands[0] if len(operands) == 1 else operands[2 if value(operands[0]) else 1]
                break
            elif instruction.opcode == pyqir.Opcode.RET:
                assert value(instruction.operands[0]) == 0
                return output
            else:
                pytest.fail(f"Unexpected instruction outside the supported classical profile: {instruction}")
        else:
            pytest.fail("A basic block has no terminator")
    pytest.fail("Unexpected loop in forward-only QIR")


@pytest.mark.parametrize("target,initialized", [("quantinuum_h2_1", False), ("quantinuum_helios", True)])
def test_quantinuum_qir_uses_documented_native_and_runtime_contract(compile_qir, target, initialized):
    artifact, module = compile_qir(
        "qubit q[2]\nbit c[2]\nu1q(0.3, 0.4) q[0]\nrzz(0.2) q[0], q[1]\n"
        "c[0] = measure q[0]\nif c[0] == 1 {\nc[1] = measure q[1]\n}\n", target)
    text = artifact.program_text()
    assert "declare void @__quantum__qis__rxy__body(double, double, ptr)" in text
    assert "declare void @__quantum__qis__rzz__body(double, ptr, ptr)" in text
    assert "declare i1 @__quantum__rt__read_result(ptr readonly)" in text
    assert "__quantum__qis__read_result__body" not in text
    assert ("call void @__quantum__rt__initialize" in text) is initialized
    assert "define i64 @main()" in text and "ret i64 0" in text
    assert '"output_labeling_schema"="labeled"' in text
    assert re.search(r"declare void @__quantum__qis__mz__body\(ptr, ptr writeonly\) #\d+", text)
    assert '"irreversible"' in text
    assert not re.search(r"\b(alloca|load|store)\b", text)
    labels = re.findall(r"@output_label\d+ = private constant .*? c\"(.*?)\\00\"", text)
    assert len(labels) == 3 and len(set(labels)) == 3
    assert run_classical_control(module, {0: 0, 1: 1}) == [0, 0]
    assert run_classical_control(module, {0: 1, 1: 1}) == [1, 1]


def test_nested_qir_joins_preserve_overwritten_predicate_and_sparse_bits(compile_qir):
    artifact, module = compile_qir("""qubit q[2]
bit c[4]
c[0] = measure q[0]
if c[0] == 1 {
c[0] = measure q[1]
if c[0] == 0 {
c[1] = measure q[1]
} else {
c[1] = measure q[0]
}
} else {
c[0] = measure q[0]
}
if c[0] == 1 {
c[2] = measure q[0]
} else {
c[2] = measure q[1]
}
""")
    assert "phi i1" in artifact.program_text()
    outcomes = {0: 1, 1: 0, 2: 1, 3: 0, 4: 1, 5: 1, 6: 0}
    assert run_classical_control(module, outcomes) == [0, 1, 0, 0]
    assert run_classical_control(module, {**outcomes, 0: 0}) == [1, 0, 1, 0]
    assert run_classical_control(module, {**outcomes, 1: 1}) == [1, 0, 1, 0]


@pytest.mark.parametrize("kind", ["dynamic", "quantinuum", "unsupported-instruction"])
def test_felis_rejects_unsupported_qir_before_creating_job(kind):
    ir = {"schema_version": 1, "num_qubits": 1, "num_clbits": 1,
          "instructions": [], "measurement_mapping": []}
    snapshot = {"qir_platform": "standard", "raw": {"instructions": [
        {"signature": "__quantum__qis__z__body:void (%Qubit*)"}]}}
    payload = "define i64 @main() { ret i64 0 }"
    if kind == "dynamic":
        ir["instructions"] = [{"op": "if", "qubits": [], "clbits": [0], "condition_value": 1}, {"op": "endif"}]
    elif kind == "quantinuum":
        snapshot["qir_platform"] = "quantinuum-h2"
    else:
        payload = "call void @__quantum__qis__x__body(ptr null)"
    artifact = CompiledArtifact("alicebob", "boson", "qir-text", payload, ir, snapshot)
    adapter = get_adapter("alicebob", {"api_key": "unused", "_transport": lambda *a, **k: pytest.fail("Created a job before validating QIR")})
    with pytest.raises(QStackError) as error:
        adapter.submit(artifact, SubmissionOptions(mode="live"))
    assert error.value.code in {"UNSUPPORTED_FORMAT", "UNSUPPORTED_CAPABILITY", "UNSUPPORTED_GATE"}
