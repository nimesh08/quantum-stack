"""Public per-artifact verification of actual pure transport construction.

Socket/authentication/transpiler traps ensure these tests cannot send a job.
Mutations change the shared builder result while stored IR/program stay intact.
"""
import copy
from dataclasses import replace
import importlib
import json
import os
import socket

import pytest

from qstack.models import CompiledArtifact, QStackError
from qstack.providers import submission_objects as builders
from qstack.providers.native import serialize_native
from qstack.verification import verify_artifact


@pytest.fixture(autouse=True)
def offline(monkeypatch):
    def forbidden(*args, **kwargs):
        pytest.fail("Artifact verification attempted authentication, network, or submission")
    for name in ("connect", "connect_ex"):
        monkeypatch.setattr(socket.socket, name, forbidden)
    monkeypatch.setattr(socket, "create_connection", forbidden)
    from qstack.providers.cloud import IBMAdapter, GoogleAdapter, AWSAdapter, AzureAdapter
    from qstack.providers.specialized import IQMAdapter, OQCAdapter, RigettiAdapter, QuantinuumAdapter
    from qstack.providers.base import RestAdapter
    for cls in (IBMAdapter, GoogleAdapter, AzureAdapter, IQMAdapter, OQCAdapter, RigettiAdapter, QuantinuumAdapter):
        monkeypatch.setattr(cls, "client", forbidden)
    monkeypatch.setattr(AWSAdapter, "session", forbidden)
    monkeypatch.setattr(RestAdapter, "request", forbidden)


def sdk(route, name):
    selected = os.environ.get("QSTACK_TEST_SDK")
    if selected and selected != route:
        pytest.skip(f"SDK matrix selected {selected}")
    return importlib.import_module(name) if selected else pytest.importorskip(name)


def artifact(route, operations=None):
    operations = operations or [{"op": "rx", "qubits": [0], "params": [.37]},
                                {"op": "cx", "qubits": [0, 1]}]
    operations = operations + [{"op": "measure", "qubits": [0], "clbits": [2]},
                               {"op": "measure", "qubits": [1], "clbits": [0]}]
    ir = {"schema_version": 2, "num_qubits": 2, "num_clbits": 3, "instructions": operations,
          "measurement_mapping": [{"qubit": 0, "clbit": 2}, {"qubit": 1, "clbit": 0}]}
    return CompiledArtifact(route, "offline", "json", json.dumps(ir), ir,
        {"qubit_labels": ["5_2", "5_3"], "qubit_ids": ["physical-a", "physical-b"]},
        schema_version=2, logical_ir=copy.deepcopy(ir))


def checks(value, **kwargs):
    evidence = verify_artifact(value, store=False, **kwargs)
    return evidence, {item["name"]: item for item in evidence["checks"]}


@pytest.mark.parametrize("mutation", [None, "angle", "operands", "readout", "unknown"])
def test_public_ibm_actual_object_mutations(mutation, monkeypatch):
    qiskit = sdk("ibm", "qiskit")
    monkeypatch.setattr(qiskit, "transpile", lambda *a, **k: pytest.fail("transpiler called"))
    original = builders.ibm_circuit
    def build(value):
        circuit = original(value)
        if mutation == "angle":
            circuit.data[0] = circuit.data[0].replace(operation=qiskit.circuit.library.RXGate(.47))
        elif mutation == "operands":
            circuit.data[1] = circuit.data[1].replace(qubits=tuple(reversed(circuit.data[1].qubits)))
        elif mutation == "readout":
            del circuit.data[-1]
        elif mutation == "unknown":
            circuit.append(qiskit.circuit.Instruction("uninterpreted", 1, 0, []), [0])
        return circuit
    monkeypatch.setattr(builders, "ibm_circuit", build)
    evidence, result = checks(artifact("ibm"))
    assert result["logical_to_physical"]["status"] == result["physical_to_program"]["status"] == "passed"
    assert result["physical_to_submission"]["status"] == ("passed" if mutation is None else "not_checked" if mutation == "unknown" else "failed"), evidence


def test_missing_sdk_retains_available_checks_and_install_reason(monkeypatch):
    def missing(value):
        raise QStackError("Install the qstack 'ibm' optional dependency to use this route", "MISSING_DEPENDENCY")
    monkeypatch.setattr(builders, "ibm_circuit", missing)
    evidence, result = checks(artifact("ibm"))
    assert evidence["status"] == "not_checked" and not evidence["coverage_complete"]
    assert all(result[key]["status"] == "passed" for key in ("logical_to_physical", "physical_to_program"))
    assert result["physical_to_submission"]["code"] == "MISSING_DEPENDENCY"
    assert "'ibm' optional dependency" in result["physical_to_submission"]["reason"]


@pytest.mark.parametrize("mutation", [None, "angle", "operands", "readout"])
def test_public_google_actual_circuit_and_engine_roundtrip(mutation, monkeypatch):
    cirq = sdk("google", "cirq")
    sdk("google", "cirq_google")
    value = artifact("google", [{"op": "phased_xz", "qubits": [0], "params": [.37, -.41, .62]},
                                 {"op": "sqrt_iswap_inv", "qubits": [0, 1]}])
    original = builders.google_circuit
    def build(value, config=None):
        circuit = original(value, config)
        operations = list(circuit.all_operations())
        if mutation == "angle": operations[0] = cirq.PhasedXZGate(x_exponent=.4, z_exponent=.2, axis_phase_exponent=.1)(*operations[0].qubits)
        elif mutation == "operands": operations[0] = operations[0].with_qubits(cirq.GridQubit(5, 3))
        elif mutation == "readout": operations.pop()
        return cirq.Circuit(operations)
    monkeypatch.setattr(builders, "google_circuit", build)
    evidence, result = checks(value, threshold=2e-6)
    assert result["physical_to_submission"]["status"] == ("passed" if mutation is None else "failed"), evidence
    # A modified builder is detected at the IR/object boundary. The protobuf
    # stage independently measures rounding of that actual object, not the IR.
    assert result["submission_to_engine_protobuf"]["status"] == ("failed" if mutation == "readout" else "passed"), evidence
    if mutation is None:
        _, strict = checks(value, threshold=2e-13)
        assert strict["physical_to_submission"]["status"] == "passed"
        assert strict["submission_to_engine_protobuf"]["status"] == "failed"
        assert strict["submission_to_engine_protobuf"]["threshold_policy"] == "user threshold unchanged"


@pytest.mark.parametrize("mutation", [None, "angle", "operands", "readout", "unknown"])
def test_public_aqt_actual_rest_body(mutation, monkeypatch):
    value = artifact("aqt", [{"op": "u1q", "qubits": [0], "params": [.37, .2]},
                             {"op": "rxx", "qubits": [0, 1], "params": [.41]}])
    original = builders.aqt_body
    def build(value, **kwargs):
        body = original(value, **kwargs)
        ops = body["payload"]["circuits"][0]["quantum_circuit"]
        if mutation == "angle": ops[0]["theta"] += .1
        elif mutation == "operands": ops[0]["qubit"] = 1
        elif mutation == "readout": ops.pop()
        elif mutation == "unknown": ops.insert(0, {"operation": "UNKNOWN"})
        return body
    monkeypatch.setattr(builders, "aqt_body", build)
    evidence, result = checks(value)
    assert result["physical_to_submission"]["status"] == ("passed" if mutation is None else "not_checked" if mutation == "unknown" else "failed"), evidence


@pytest.mark.parametrize("mutation", [None, "angle", "operands", "readout"])
def test_public_iqm_actual_sdk_values(mutation, monkeypatch):
    sdk("iqm", "iqm.pulse.circuit_operations")
    value = artifact("iqm", [{"op": "u1q", "qubits": [0], "params": [.37, .2]}, {"op": "cz", "qubits": [0, 1]}])
    value.format, value.payload = serialize_native("iqm", value.physical_ir, value.target_snapshot)
    original = builders.iqm_circuit
    def build(value, name="qstack"):
        data = json.loads(value.program_text())
        ops = data["instructions"]
        if mutation == "angle": ops[0]["args"]["angle"] += .1
        elif mutation == "operands": ops[0]["locus"] = [value.target_snapshot["qubit_labels"][1]]
        elif mutation == "readout": ops.pop()
        return original(replace(value, payload=json.dumps(data)), name)
    monkeypatch.setattr(builders, "iqm_circuit", build)
    evidence, result = checks(value)
    assert result["physical_to_program"]["status"] == "passed"
    assert result["physical_to_submission"]["status"] == ("passed" if mutation is None else "failed"), evidence


@pytest.mark.parametrize("mutation", [None, "angle", "operands", "readout"])
def test_public_qibolab_actual_assembler_plan_and_pulse_boundary(mutation, monkeypatch):
    from qstack.providers import qibolab_native
    value = artifact("qibolab", [{"op": "u1q", "qubits": [0], "params": [.37, .2]}, {"op": "cx", "qubits": [0, 1]}])
    original = qibolab_native.native_plan
    def build(value):
        plan = original(value)
        if mutation == "angle": plan[0]["parameters"][0] += .1
        elif mutation == "operands": plan[1]["physical_ids"].reverse()
        elif mutation == "readout": plan.pop()
        return plan
    monkeypatch.setattr(qibolab_native, "native_plan", build)
    monkeypatch.setattr(qibolab_native, "load_platform", lambda *a: pytest.fail("Verifier loaded executable platform configuration"))
    evidence, result = checks(value)
    assert result["physical_to_submission"]["status"] == ("passed" if mutation is None else "failed"), evidence
    assert result["submission_to_calibrated_pulses"]["status"] == "not_checked"
    assert evidence["status"] == ("not_checked" if mutation is None else "failed")


def test_ibm_result_register_name_is_part_of_transport_contract(monkeypatch):
    qiskit = sdk("ibm", "qiskit")
    original = builders.ibm_circuit
    def build(value):
        changed = qiskit.QuantumCircuit(2)
        changed.add_register(qiskit.ClassicalRegister(3, "wrong_name"))
        changed.compose(original(value), inplace=True)
        return changed
    monkeypatch.setattr(builders, "ibm_circuit", build)
    assert checks(artifact("ibm"))[1]["physical_to_submission"]["status"] == "failed"


def test_ibm_register_condition_returns_not_checked_instead_of_sdk_exception(monkeypatch):
    sdk("ibm", "qiskit")
    original = builders.ibm_circuit
    def build(value):
        circuit = original(value)
        with circuit.if_test((circuit.cregs[0], 1)):
            circuit.x(0)
        return circuit
    monkeypatch.setattr(builders, "ibm_circuit", build)
    evidence, result = checks(artifact("ibm"))
    assert result["physical_to_submission"]["status"] == "not_checked", evidence
    assert "register conditions" in result["physical_to_submission"]["reason"]


@pytest.mark.parametrize("wrapped", [False, True])
def test_google_readout_noise_and_wrapped_measurement_fail_closed(wrapped, monkeypatch):
    cirq = sdk("google", "cirq")
    import numpy as np
    original = builders.google_circuit
    def build(value, config=None):
        operations = list(original(value, config).all_operations())
        measurement = operations[-1]
        operations[-1] = (cirq.CircuitOperation(cirq.FrozenCircuit(measurement)) if wrapped else
            cirq.MeasurementGate(1, key=cirq.measurement_key_name(measurement),
                confusion_map={(0,): np.array([[.9, .1], [.2, .8]])})(*measurement.qubits))
        return cirq.Circuit(operations)
    monkeypatch.setattr(builders, "google_circuit", build)
    evidence, result = checks(artifact("google", [{"op": "u1q", "qubits": [0], "params": [.37, .2]}]), threshold=2e-6)
    assert result["physical_to_submission"]["status"] == "not_checked", evidence
    assert "not skipped" in result["physical_to_submission"]["reason"]


@pytest.mark.parametrize("route,module", [("google", "cirq"), ("iqm", "iqm.pulse.circuit_operations")])
@pytest.mark.parametrize("mutation", ["swap", "extra", "drop"])
def test_actual_receipt_key_mapping_mutation_is_detected(route, module, mutation, monkeypatch):
    sdk(route, module)
    value = artifact(route, [{"op": "u1q", "qubits": [0], "params": [.37, .2]}, {"op": "cz", "qubits": [0, 1]}])
    if route == "iqm":
        value.format, value.payload = serialize_native(route, value.physical_ir, value.target_snapshot)
    original = getattr(builders, route + "_readout")
    def wrong(*args):
        mapping = original(*args)
        if mutation == "swap": mapping[0]["clbit"], mapping[1]["clbit"] = mapping[1]["clbit"], mapping[0]["clbit"]
        elif mutation == "extra": mapping.append({"key": "c1_missing", "clbit": 1})
        else: mapping.pop()
        return mapping
    monkeypatch.setattr(builders, route + "_readout", wrong)
    evidence, result = checks(value, threshold=2e-6)
    assert result["physical_to_submission"]["status"] == "failed", evidence


@pytest.mark.parametrize("width", ["num_qubits", "num_clbits"])
def test_pathological_interface_width_never_constructs_sdk_objects(width, monkeypatch):
    value = artifact("ibm")
    value.physical_ir[width] = 2**60
    monkeypatch.setattr(builders, "ibm_circuit", lambda *a: pytest.fail("Unbounded SDK allocation"))
    evidence, result = checks(value)
    assert result["physical_to_submission"]["status"] == "not_checked", evidence
    assert "allocation budget" in result["physical_to_submission"]["reason"]


def test_active_quantum_budget_checked_before_sdk_object_construction(monkeypatch):
    value = artifact("ibm")
    value.physical_ir["num_qubits"] = 127
    monkeypatch.setattr(builders, "ibm_circuit", lambda *a: pytest.fail("Out-of-domain SDK object construction"))
    assert checks(value)[1]["physical_to_submission"]["status"] == "not_checked"


def test_sparse_127_qubit_interface_with_two_active_wires_is_accepted():
    sdk("ibm", "qiskit")
    value = artifact("ibm")
    ir = value.physical_ir
    ir["num_qubits"], ir["initial_logical_to_physical"], ir["logical_to_physical"] = 127, [17, 92], [17, 92]
    for instruction in ir["instructions"]:
        instruction["qubits"] = [[17, 92][q] for q in instruction["qubits"]]
    value.logical_ir, value.payload = copy.deepcopy(ir), json.dumps(ir)
    evidence, result = checks(value)
    assert result["physical_to_submission"]["status"] == "passed", evidence


def test_google_receipt_order_controls_final_writes_not_circuit_iteration(monkeypatch):
    cirq = sdk("google", "cirq")
    value = artifact("google", [{"op": "u1q", "qubits": [0], "params": [.37, .2]}])
    value.physical_ir["instructions"][-1]["clbits"] = [2]
    value.physical_ir["measurement_mapping"][-1]["clbit"] = 2
    value.logical_ir, value.payload = copy.deepcopy(value.physical_ir), json.dumps(value.physical_ir)
    original = builders.google_circuit
    def reordered(artifact, config=None):
        ops = list(original(artifact, config).all_operations())
        # Readouts on disjoint wires commute quantum mechanically. The receipt
        # selects c2__1 as the final write, independent of this iteration order.
        ops[-2:] = reversed(ops[-2:])
        return cirq.Circuit(cirq.Moment([op]) for op in ops)
    monkeypatch.setattr(builders, "google_circuit", reordered)
    assert checks(value, threshold=2e-6)[1]["physical_to_submission"]["status"] == "passed"
    def renamed(artifact, config=None):
        ops = list(original(artifact, config).all_operations())
        ops[-1] = cirq.measure(*ops[-1].qubits, key="c2__77")
        return cirq.Circuit(ops)
    monkeypatch.setattr(builders, "google_circuit", renamed)
    assert checks(value, threshold=2e-6)[1]["physical_to_submission"]["status"] == "failed"


@pytest.mark.parametrize("mutation", ["angle", "operands", "readout"])
def test_google_protobuf_mutation_isolated_from_correct_sdk_circuit(mutation, monkeypatch):
    cirq = sdk("google", "cirq")
    sdk("google", "cirq_google")
    value = artifact("google", [{"op": "phased_xz", "qubits": [0], "params": [.37, -.41, .62]},
                                 {"op": "sqrt_iswap_inv", "qubits": [0, 1]}])
    original = builders.google_protobuf
    def changed(circuit):
        ops = list(circuit.all_operations())
        if mutation == "angle": ops[0] = cirq.PhasedXZGate(x_exponent=.4, z_exponent=.2, axis_phase_exponent=.1)(*ops[0].qubits)
        elif mutation == "operands": ops[0] = ops[0].with_qubits(cirq.GridQubit(5, 3))
        else: ops.pop()
        return original(cirq.Circuit(ops))
    monkeypatch.setattr(builders, "google_protobuf", changed)
    evidence, result = checks(value, threshold=2e-6)
    assert result["physical_to_submission"]["status"] == "passed", evidence
    assert result["submission_to_engine_protobuf"]["status"] == "failed", evidence


@pytest.mark.parametrize("route,module,builder", [("aws", "braket.ir.openqasm", "aws_program"), ("oqc", "qcaas_client.client", "oqc_task")])
@pytest.mark.parametrize("mutation", [None, "angle", "operands", "readout"])
def test_public_actual_text_sdk_wrappers(route, module, builder, mutation, monkeypatch):
    sdk(route, module)
    value = artifact(route)
    value.format = "qasm3"
    value.payload = 'OPENQASM 3.0; qubit[2] q; bit[3] c; rx(0.37) q[0]; cx q[0],q[1]; c[2] = measure q[0]; c[0] = measure q[1];'
    original = getattr(builders, builder)
    def build(value):
        payload = value.program_text()
        if mutation == "angle": payload = payload.replace("0.37", "0.47")
        elif mutation == "operands": payload = payload.replace("cx q[0],q[1]", "cx q[1],q[0]")
        elif mutation == "readout": payload = payload.replace("c[0] = measure q[1];", "")
        return original(replace(value, payload=payload))
    monkeypatch.setattr(builders, builder, build)
    evidence, result = checks(value)
    assert result["physical_to_program"]["status"] == "passed"
    assert result["physical_to_submission"]["status"] == ("passed" if mutation is None else "failed"), evidence


def test_unknown_route_and_aqt_partial_quantum_output_are_not_certified():
    evidence, result = checks(artifact("uncontracted"))
    assert result["physical_to_submission"]["status"] == "not_checked" and not evidence["certified"]
    value = artifact("aqt", [{"op": "rz", "qubits": [0], "params": [.37]}])
    value.physical_ir["measurement_mapping"].pop()
    assert checks(value)[1]["physical_to_submission"]["status"] == "not_checked"


@pytest.mark.parametrize("route", ["ionq", "anyon"])
@pytest.mark.parametrize("mutation", [None, "angle", "operands", "readout"])
def test_actual_native_json_rest_body_is_independently_interpreted(route, mutation, monkeypatch):
    operations = ([{"op": "gpi2", "qubits": [0], "params": [.37]}, {"op": "rzz", "qubits": [0, 1], "params": [.41]}]
        if route == "ionq" else [{"op": "sx", "qubits": [0]}, {"op": "rz", "qubits": [0], "params": [.37]},
                                 {"op": "sx", "qubits": [0]}, {"op": "cz", "qubits": [0, 1]}])
    value = artifact(route, operations)
    value.format, value.payload = serialize_native(route, value.physical_ir, value.target_snapshot)
    name = route + "_body"
    original = getattr(builders, name)
    def build(value):
        body = original(value)
        if route == "ionq":
            if mutation == "angle": body["input"]["circuit"][0]["phase"] += .1
            elif mutation == "operands": body["input"]["circuit"][0]["target"] = 1
            elif mutation == "readout":
                # IonQ readout is implicit. Removing the all-qubit dimension
                # must fail instead of manufacturing the missing output wire.
                body["input"]["qubits"] = 1
        else:
            ops = body["circuit"]["operations"]
            if mutation == "angle": ops[1]["parameters"]["lambda"] += .1
            elif mutation == "operands": ops[0]["qubits"] = [1]
            elif mutation == "readout": ops.pop()
        return body
    monkeypatch.setattr(builders, name, build)
    evidence, result = checks(value)
    assert result["physical_to_program"]["status"] == "passed"
    assert result["physical_to_submission"]["status"] == ("passed" if mutation is None else "failed"), evidence


@pytest.mark.parametrize("route", ["rigetti", "azure"])
@pytest.mark.parametrize("mutation", [None, "angle", "operands", "readout"])
def test_raw_text_upload_uses_shared_payload_builder(route, mutation, monkeypatch):
    value = artifact(route)
    if route == "rigetti":
        value.format, value.payload = "quil", "DECLARE ro BIT[3]\nRX(0.37) 0\nCNOT 0 1\nMEASURE 0 ro[2]\nMEASURE 1 ro[0]\n"
    else:
        value.format = "qasm2"
        value.payload = 'OPENQASM 2.0; qreg q[2]; creg c[3]; rx(0.37) q[0]; cx q[0],q[1]; measure q[0] -> c[2]; measure q[1] -> c[0];'
    original = builders.upload_payload
    def build(value):
        text = original(value)
        if mutation == "angle": text = text.replace("0.37", "0.47")
        elif mutation == "operands": text = text.replace("CNOT 0 1", "CNOT 1 0").replace("cx q[0],q[1]", "cx q[1],q[0]")
        elif mutation == "readout": text = text.replace("MEASURE 1 ro[0]", "").replace("measure q[1] -> c[0];", "")
        return text
    monkeypatch.setattr(builders, "upload_payload", build)
    evidence, result = checks(value)
    assert result["physical_to_program"]["status"] == "passed"
    assert result["physical_to_submission"]["status"] == ("passed" if mutation is None else "failed"), evidence


@pytest.mark.parametrize("route", ["quantinuum", "alicebob", "azure", "oqc"])
@pytest.mark.parametrize("mutation", [None, "angle", "operands", "readout"])
def test_qir_payload_upload_or_task_roundtrip(route, mutation, monkeypatch):
    pyqir = pytest.importorskip("pyqir")
    if route == "oqc": sdk(route, "qcaas_client.client")
    value = artifact(route)
    text = '''
declare void @__quantum__qis__rx__body(double, ptr)
declare void @__quantum__qis__cnot__body(ptr, ptr)
declare void @__quantum__qis__mz__body(ptr, ptr)
declare void @__quantum__rt__array_record_output(i64, ptr)
declare void @__quantum__rt__bool_record_output(i1, ptr)
declare void @__quantum__rt__result_record_output(ptr, ptr)
define i64 @main() {
entry:
 call void @__quantum__qis__rx__body(double 3.700000e-1, ptr null)
 call void @__quantum__qis__cnot__body(ptr null, ptr inttoptr (i64 1 to ptr))
 call void @__quantum__qis__mz__body(ptr null, ptr null)
 call void @__quantum__qis__mz__body(ptr inttoptr (i64 1 to ptr), ptr inttoptr (i64 1 to ptr))
 call void @__quantum__rt__array_record_output(i64 3, ptr null)
 call void @__quantum__rt__result_record_output(ptr inttoptr (i64 1 to ptr), ptr null)
 call void @__quantum__rt__bool_record_output(i1 false, ptr null)
 call void @__quantum__rt__result_record_output(ptr null, ptr null)
 ret i64 0
}
'''
    def encode(text):
        return text if route == "alicebob" else pyqir.Module.from_ir(pyqir.Context(), text).bitcode
    value.format, value.payload = ("qir-text" if route == "alicebob" else "qir-bitcode"), encode(text)
    name = "oqc_task" if route == "oqc" else "upload_payload"
    original = getattr(builders, name)
    def build(value):
        changed = text
        if mutation == "angle": changed = changed.replace("3.700000e-1", "4.700000e-1")
        elif mutation == "operands": changed = changed.replace("cnot__body(ptr null, ptr inttoptr (i64 1 to ptr))", "cnot__body(ptr inttoptr (i64 1 to ptr), ptr null)")
        elif mutation == "readout": changed = changed.replace(" call void @__quantum__rt__result_record_output(ptr null, ptr null)", "")
        return original(replace(value, payload=encode(changed)))
    monkeypatch.setattr(builders, name, build)
    evidence, result = checks(value)
    assert result["physical_to_program"]["status"] == "passed", evidence
    assert result["physical_to_submission"]["status"] == ("passed" if mutation is None else "failed"), evidence
