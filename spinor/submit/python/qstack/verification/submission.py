"""Interpret per-artifact submission values without constructing any clients."""
from __future__ import annotations

import base64
from dataclasses import replace
import json

from . import NotChecked
from .parsers import decode
from .sdk import aqt_body, cirq_object, iqm_object, qibolab_plan, qiskit_object
from ..providers import submission_objects as builders


SCALAR_PHASE = "Sampling transport omits scalar phase; complete instruments checked instead"


def _payload(artifact, value, fmt=None):
    if not isinstance(value, (str, bytes)):
        value = json.dumps(value, allow_nan=False)
    return decode(replace(artifact, payload=value, format=fmt or artifact.format))


def construct(artifact):
    """The same pure builder each adapter calls before its remote submission."""
    route = artifact.route
    if route == "ibm": return builders.ibm_circuit(artifact)
    if route == "google": return builders.google_circuit(artifact)
    if route == "aws": return builders.aws_program(artifact)
    if route == "iqm": return builders.iqm_circuit(artifact)
    if route == "oqc": return builders.oqc_task(artifact)
    if route == "aqt": return builders.aqt_body(artifact)
    if route == "ionq": return builders.ionq_body(artifact)
    if route == "anyon": return builders.anyon_body(artifact)
    if route == "qibolab":
        from ..providers.qibolab_native import native_plan
        return native_plan(artifact)
    if route in {"quantinuum", "azure", "rigetti", "alicebob"}:
        return builders.upload_payload(artifact)
    raise NotChecked(f"No independent submission-object contract for route {route}")


def interpret(artifact, value):
    route, ir = artifact.route, artifact.physical_ir
    if route == "ionq" or (route == "azure" and artifact.format in {"ionq-json", "ionq-native-json"}):
        if {m["qubit"] for m in ir.get("measurement_mapping", [])} != set(range(ir["num_qubits"])):
            raise NotChecked("IonQ implicit all-qubit readout with partially retained quantum outputs is outside this instrument oracle")
    if route == "ibm": return qiskit_object(value, ir), []
    if route == "google": return cirq_object(value, ir, artifact.target_snapshot.get("qubit_labels", []), builders.google_readout(artifact)), [SCALAR_PHASE]
    if route == "iqm": return iqm_object(value, ir, artifact.target_snapshot.get("qubit_labels", []), builders.iqm_readout(artifact, value)), [SCALAR_PHASE]
    if route == "aqt": return aqt_body(value, ir), [SCALAR_PHASE]
    if route == "qibolab":
        return qibolab_plan(value, ir, artifact.target_snapshot.get("qubit_ids", [])), [SCALAR_PHASE]
    if route == "aws": return _payload(artifact, value.source, "qasm3")
    if route == "oqc":
        payload = value.program if artifact.format.lower() in {"qasm2", "openqasm2", "qasm3", "openqasm3"} else base64.b64decode(value.program, validate=True)
        return _payload(artifact, payload)
    if route == "ionq": return _payload(artifact, value["input"], "ionq-native-json")
    if route == "anyon": return _payload(artifact, value["circuit"], "anyon-json")
    if route in {"quantinuum", "azure", "rigetti", "alicebob"}: return _payload(artifact, value)
    raise NotChecked(f"No independent submission-object interpreter for route {route}")


def google_roundtrip(artifact, circuit):
    # This is the pinned Engine default serializer, also used in our offline
    # pre-submit serialization check. It cannot contact Engine or discover devices.
    proto = builders.google_protobuf(circuit)
    from ..providers.base import optional
    decoded = optional("cirq_google.serialization.circuit_serializer", "google").CircuitSerializer().deserialize(proto)
    return cirq_object(decoded, artifact.physical_ir, artifact.target_snapshot.get("qubit_labels", []), builders.google_readout(artifact)), [SCALAR_PHASE]
