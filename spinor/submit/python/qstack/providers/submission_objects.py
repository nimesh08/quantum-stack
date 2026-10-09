"""Pure submission values shared by live adapters and offline verification.

These functions never construct a service, load credentials, discover a target,
transpile, or submit. Keep all remote operations in the adapters.
"""
from __future__ import annotations

import base64
import json

from qstack.models import QStackError
from .base import optional
from . import native


def ibm_circuit(artifact):
    return native.qiskit_circuit(artifact)


def google_circuit(artifact, config=None):
    return native.cirq_circuit(artifact, config or {})


def google_readout(artifact):
    return native.cirq_measurement_keys(artifact.physical_ir)


def google_protobuf(circuit):
    return optional("cirq_google.serialization.circuit_serializer", "google").CircuitSerializer().serialize(circuit)


def aws_program(artifact):
    return optional("braket.ir.openqasm", "aws").Program(source=artifact.program_text())


def iqm_circuit(artifact, name="qstack"):
    data = json.loads(artifact.program_text())
    circuits = data.get("circuits", [data])
    if len(circuits) != 1:
        raise QStackError("One compiled artifact must contain exactly one IQM circuit", "ARTIFACT_INVALID")
    operation = optional("iqm.pulse.builder", "iqm").CircuitOperation
    circuit = optional("iqm.pulse.circuit_operations", "iqm").Circuit
    return circuit(name=name, instructions=tuple(operation(name=i["name"], locus=tuple(i["locus"]),
        args=i.get("args", {})) for i in circuits[0]["instructions"]))


def iqm_readout(artifact, circuit):
    measurements = [i for i in artifact.physical_ir.get("instructions", []) if i.get("op") == "measure"]
    keys = [i.args["key"] for i in circuit.instructions if i.name == "measure"]
    if len(keys) != len(measurements) or len(set(keys)) != len(keys):
        raise QStackError("IQM circuit readout keys differ from the compiled readout contract", "ARTIFACT_INVALID")
    return [{"key": key, "clbit": inst["clbits"][0]} for key, inst in zip(keys, measurements)]


def oqc_task(artifact, *, shots=1, target=None):
    compiler = optional("compiler_config.config", "oqc")
    optim = compiler.Tket()
    optim.disable()
    config = compiler.CompilerConfig(repeats=shots, optimizations=optim,
        results_format=compiler.QuantumResultsFormat().binary_count())
    program = (artifact.program_text() if artifact.format.lower() in {"qasm2", "openqasm2", "qasm3", "openqasm3"}
        else base64.b64encode(artifact.program_bytes()).decode("ascii"))
    return optional("qcaas_client.client", "oqc").QPUTask(program=program, qpu_id=target or artifact.target, config=config)


def aqt_body(artifact, *, shots=1, name="qstack"):
    from .aqt import aqt_operations
    if shots > 2000:
        raise QStackError("AQT cloud supports at most 2000 shots per circuit", "INVALID_OPTIONS")
    return {"job_type": "quantum_circuit", "label": name, "payload": {"circuits": [{
        "repetitions": shots, "quantum_circuit": aqt_operations(artifact.physical_ir),
        "number_of_qubits": artifact.physical_ir["num_qubits"]}]}}


def ionq_body(artifact, *, shots=1, name="qstack", target=None):
    circuit = json.loads(artifact.program_text())
    circuit = circuit.get("input", circuit)
    if circuit.get("gateset") != "native":
        raise QStackError("IonQ direct submission requires gateset=native; no QIS compiler fallback", "UNSUPPORTED_FORMAT")
    if any(op.get("gate") not in {"gpi", "gpi2", "ms", "zz"} for op in circuit.get("circuit", [])):
        raise QStackError("IonQ artifact contains a non-native instruction", "UNSUPPORTED_GATE")
    return {"type": "ionq.circuit.v1", "backend": target or artifact.target, "shots": shots,
        "name": name, "input": circuit, "settings": {"error_mitigation": {"debiasing": False}}}


def anyon_body(artifact, *, shots=1, name="qstack", target=None, project=None):
    circuit = json.loads(artifact.program_text())
    circuit = circuit.get("circuit", circuit)
    if not all(k in circuit for k in ("operations", "qubitCount", "bitCount")):
        raise QStackError("Anyon artifact must contain operations, qubitCount and bitCount", "ARTIFACT_INVALID")
    body = {"name": name, "type": "circuit", "machineName": target or artifact.target,
        "circuit": circuit, "shotCount": shots}
    if project:
        body["projectID"] = project
    return body


def upload_payload(artifact):
    """The unmodified program value sent to byte/text-upload provider APIs."""
    if artifact.route == "azure" and artifact.format.lower() in {"ionq-json", "ionq-native-json"}:
        return json.loads(artifact.program_text())
    if artifact.route == "quantinuum" or (artifact.route == "azure" and artifact.format.lower() in {"qir-bitcode", "qir.bc", "qir.v1"}):
        return artifact.program_bytes()
    return artifact.program_text()
